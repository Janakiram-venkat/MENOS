"""QuantumCircuit -> menos._core.Circuit conversion.

The one explicit conversion module (docs/BUILD_PLAN.md Phase 2, 2.3). This
module's job ends at producing a menos._core.Circuit; it never simulates
anything itself.
"""

from __future__ import annotations

from qiskit.circuit import QuantumCircuit
from qiskit.circuit.exceptions import CircuitError

from menos._core import Circuit, kMaxUnitaryQubits
from menos.exceptions import InvalidCircuitError, UnsupportedFeatureError
from menos.qiskit._gate_table import DROPPED_INSTRUCTION_NAMES, GATE_TABLE
from menos.qiskit.noise import (
    CHANNEL_INSTRUCTION_NAME,
    VERIFIED_TYPES,
    channel_kraus,
    verified_type,
)


def convert_circuit(
    qc: QuantumCircuit, readout_errors: dict[int, list[float]] | None = None
) -> Circuit:
    """Converts a Qiskit QuantumCircuit to a menos._core.Circuit.

    ``readout_errors`` maps a qubit index to the readout error of every
    measurement of that qubit, ``[P(0|0), P(1|0), P(0|1), P(1|1)]``
    (menos.qiskit.noise.apply_noise_model). A Qiskit Aer channel instruction
    (an inserted or appended noise error) becomes a MENOS channel operation if
    it is one of the verified noise types (docs/adr/0016 section 9.1).

    Raises InvalidCircuitError for unbound parameters and
    UnsupportedFeatureError for instructions with no KIR representation
    (reset, mid-circuit measurement, classical control, unverified noise
    types, and anything else conversion cannot lower) -- CLAUDE.md prime directive 7:
    fail loudly, never erase semantics to make a circuit executable.
    """
    circuit = Circuit(qc.num_qubits, qc.num_clbits)
    circuit.global_phase = qc.global_phase

    # qc.find_bit() is called once per operand per instruction below (a
    # profiled 100,000-operation representative circuit calls it ~150,000
    # times for as few as 10 distinct qubits/clbits -- docs/adr/0004's
    # conversion-performance measurement, spec 2.7). Building these two
    # maps once, up front, replaces every later find_bit() call with an
    # O(1) dict lookup.
    qubit_index = {qubit: i for i, qubit in enumerate(qc.qubits)}
    clbit_index = {clbit: i for i, clbit in enumerate(qc.clbits)}

    # Named-gate operations (the ~100% case for a typical circuit) are
    # accumulated here in CSR form and flushed via one Circuit.append_batch
    # call instead of one Circuit.append call per operation -- collapsing
    # O(num_operations) Python/C++ boundary crossings into O(1) (docs/adr/0004's
    # conversion-performance measurement, spec 2.7). measure/unitary/error
    # paths flush first, so operation order is preserved exactly.
    batch_opcodes: list = []
    batch_qubits: list[int] = []
    batch_qubit_offsets: list[int] = [0]
    batch_params: list[float] = []
    batch_param_offsets: list[int] = [0]
    qubit_count = 0
    param_count = 0

    def flush_batch() -> None:
        nonlocal qubit_count, param_count
        if not batch_opcodes:
            return
        circuit.append_batch(batch_opcodes, batch_qubits, batch_qubit_offsets, batch_params,
                              batch_param_offsets)
        batch_opcodes.clear()
        batch_qubits.clear()
        del batch_qubit_offsets[1:]
        batch_params.clear()
        del batch_param_offsets[1:]
        qubit_count = 0
        param_count = 0

    for index, instruction in enumerate(qc.data):
        op = instruction.operation
        name = op.name

        if name in DROPPED_INSTRUCTION_NAMES:
            continue

        if name == "measure":
            flush_batch()
            qubit = qubit_index[instruction.qubits[0]]
            clbit = clbit_index[instruction.clbits[0]]
            if readout_errors is not None and qubit in readout_errors:
                circuit.append_measure(qubit, clbit, readout_errors[qubit])
            else:
                circuit.append_measure(qubit, clbit)
            continue

        if name == CHANNEL_INSTRUCTION_NAME:
            flush_batch()
            qubits = [qubit_index[q] for q in instruction.qubits]
            kraus = channel_kraus(op)
            if verified_type(kraus, len(qubits)) is None:
                raise UnsupportedFeatureError(
                    f"operation {index} ({name}) on qubits {qubits}: this noise channel is not one "
                    f"of the verified noise types ({VERIFIED_TYPES}); other types are not "
                    "supported yet"
                )
            circuit.append_channel(qubits, [k.flatten(order="C").tolist() for k in kraus])
            continue

        if op.is_parameterized():
            raise InvalidCircuitError(
                f"operation {index} ({name}): unbound parameter(s) {op.params}; bind all "
                "parameters before running (QuantumCircuit.assign_parameters)"
            )

        qubits = [qubit_index[q] for q in instruction.qubits]
        spec = GATE_TABLE.get(name)

        if spec is not None:
            if len(qubits) != spec.arity:
                raise InvalidCircuitError(
                    f"operation {index} ({name}): expected {spec.arity} qubit(s), got "
                    f"{len(qubits)}"
                )
            batch_opcodes.append(spec.opcode)
            batch_qubits.extend(qubits)
            qubit_count += len(qubits)
            batch_qubit_offsets.append(qubit_count)
            if op.params:
                batch_params.extend(float(p) for p in op.params)
                param_count += len(op.params)
            batch_param_offsets.append(param_count)
            continue

        if len(qubits) <= kMaxUnitaryQubits:
            flush_batch()
            try:
                matrix = op.to_matrix()
            except (AttributeError, CircuitError) as exc:
                raise UnsupportedFeatureError(
                    f"operation {index} ({name}): not a MENOS-supported named gate and has no "
                    "matrix representation; unsupported. Try transpile(qc, backend) first."
                ) from exc
            # menos._core.Circuit.append_unitary expects a flat row-major
            # sequence (matching include/menos/circuit.h's UnitaryOp), not
            # a 2D array; op.to_matrix() returns a 2D numpy array.
            circuit.append_unitary(qubits, matrix.flatten(order="C").tolist())
            continue

        raise UnsupportedFeatureError(
            f"operation {index} ({name}): acts on {len(qubits)} qubits, exceeding "
            f"kMaxUnitaryQubits={kMaxUnitaryQubits}, and is not a MENOS-supported named gate. "
            "Try transpile(qc, backend) first."
        )

    flush_batch()
    return circuit
