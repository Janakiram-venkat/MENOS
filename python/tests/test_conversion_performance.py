"""docs/BUILD_PLAN.md Phase 2, 2.7: measure conversion separately from
simulation using a representative 100,000-operation circuit and at least
one smaller circuit. Reported, not gated -- optimization is only justified
if conversion exceeds 10% of total runtime, which this test's job is to
show one way or the other, not to assume.
"""

from __future__ import annotations

import time

from qiskit import QuantumCircuit

from menos.qiskit.convert import convert_circuit


def _build_circuit(num_qubits: int, num_ops: int) -> QuantumCircuit:
    qc = QuantumCircuit(num_qubits, num_qubits)
    for i in range(num_ops):
        q0 = i % num_qubits
        if i % 2 == 0:
            qc.h(q0)
        else:
            q1 = (i + 1) % num_qubits
            qc.cx(q0, q1)
    qc.measure(range(num_qubits), range(num_qubits))
    return qc


def _measure(backend, num_qubits: int, num_ops: int) -> None:
    qc = _build_circuit(num_qubits, num_ops)

    t0 = time.perf_counter()
    circuit = convert_circuit(qc)
    t1 = time.perf_counter()
    conversion_s = t1 - t0

    result = backend.run(qc, shots=1).result()
    t2 = time.perf_counter()
    total_submission_s = t2 - t1

    execute_s = result.results[0].time_taken
    del circuit  # only needed to time conversion in isolation above

    ratio = conversion_s / total_submission_s if total_submission_s > 0 else float("nan")
    print(
        f"[ REPORT ] conversion_performance num_qubits={num_qubits} num_ops={num_ops} "
        f"conversion_s={conversion_s:.6f} total_submission_s={total_submission_s:.6f} "
        f"cpp_execute_s={execute_s:.6f} conversion_ratio={ratio:.4f}"
    )


def test_conversion_performance_small(backend):
    _measure(backend, num_qubits=5, num_ops=200)


def test_conversion_performance_representative(backend):
    _measure(backend, num_qubits=10, num_ops=100_000)
