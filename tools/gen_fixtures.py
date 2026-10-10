#!/usr/bin/env python3
"""Generates tests/ir/fixtures/random_circuits.json.

docs/BUILD_PLAN.md Phase 1, 1.6: "tools/gen_fixtures.py (Qiskit version
pinned in tools/requirements.txt) generates about 100 random circuits of 2
to 10 qubits over the full opcode set including Unitary and random
global_phase, with expected statevectors from
qiskit.quantum_info.Statevector. Confirm that the oracle includes
global_phase. If it does not, the generator applies it explicitly and says
so."

Dev-only: this script and its Qiskit dependency are never a build or
runtime dependency of the compiled C++ library (CLAUDE.md's repo-layout
line for tools/: "development scripts, such as the fixture generator").
Run once, from a venv:

    python3 -m venv tools/.venv
    tools/.venv/bin/pip install -r tools/requirements.txt
    tools/.venv/bin/python tools/gen_fixtures.py
"""

import json
import sys

import numpy as np
import qiskit
from qiskit import QuantumCircuit, qasm2
from qiskit.quantum_info import Pauli, SparsePauliOp, Statevector

SEED = 20260921  # fixed, so the fixture set is reproducible
NUM_CIRCUITS = 100
MIN_QUBITS = 2
MAX_QUBITS = 10
MAX_UNITARY_QUBITS = 5  # kMaxUnitaryQubits, include/menos/circuit.h
MIN_OPS = 5
MAX_OPS = 20
MIN_OBSERVABLE_TERMS = 1
MAX_OBSERVABLE_TERMS = 3
OUTPUT_PATH = "tests/ir/fixtures/random_circuits.json"

ZERO_PARAM_1Q_GATES = ["x", "y", "z", "h", "s", "sdg", "t", "tdg", "sx", "sxdg"]
PARAM_1Q_GATES = {"rx": 1, "ry": 1, "rz": 1, "p": 1, "u": 3}
ZERO_PARAM_2Q_GATES = ["cx", "cz", "swap"]
PARAM_2Q_GATES = {"cp": 1}


def confirm_global_phase_is_tracked():
    """Verify the oracle includes global_phase before trusting it for every
    fixture; if this ever fails, this generator must apply it explicitly
    (per the spec quoted above), which it currently does not."""
    qc = QuantumCircuit(1)
    qc.global_phase = np.pi / 3
    actual = Statevector(qc).data[0]
    expected = np.exp(1j * np.pi / 3)
    if abs(actual - expected) > 1e-9:
        raise RuntimeError(
            "qiskit.quantum_info.Statevector does not track circuit.global_phase as "
            f"expected (installed qiskit {qiskit.__version__}): got {actual}, expected "
            f"{expected}. gen_fixtures.py must be updated to apply global_phase "
            "explicitly before this fixture set can be trusted."
        )
    print(
        f"confirmed: Statevector(qc) tracks circuit.global_phase (qiskit {qiskit.__version__})",
        file=sys.stderr,
    )


def random_unitary(rng, k):
    """A Haar-random unitary of dimension 2^k, via QR decomposition of a
    complex Ginibre matrix (Mezzadri, "How to generate random matrices from
    the classical compact groups", 2007)."""
    dim = 2**k
    z = rng.normal(size=(dim, dim)) + 1j * rng.normal(size=(dim, dim))
    q, r = np.linalg.qr(z)
    d = np.diagonal(r)
    return q * (d / np.abs(d))


def build_random_circuit(rng, num_qubits):
    qc = QuantumCircuit(num_qubits)
    qc.global_phase = float(rng.uniform(0, 2 * np.pi))
    operations = []

    num_ops = int(rng.integers(MIN_OPS, MAX_OPS + 1))
    kinds = ["1q", "1qp", "2q", "2qp", "unitary"]
    if num_qubits >= 3:
        kinds.append("3q")

    for _ in range(num_ops):
        kind = rng.choice(kinds)
        if kind == "1q":
            name = str(rng.choice(ZERO_PARAM_1Q_GATES))
            q = int(rng.integers(0, num_qubits))
            getattr(qc, name)(q)
            operations.append({"opcode": name, "qubits": [q], "params": []})
        elif kind == "1qp":
            name = str(rng.choice(list(PARAM_1Q_GATES.keys())))
            params = [float(rng.uniform(-2 * np.pi, 2 * np.pi)) for _ in range(PARAM_1Q_GATES[name])]
            q = int(rng.integers(0, num_qubits))
            getattr(qc, name)(*params, q)
            operations.append({"opcode": name, "qubits": [q], "params": params})
        elif kind == "2q" and num_qubits >= 2:
            name = str(rng.choice(ZERO_PARAM_2Q_GATES))
            q0, q1 = (int(x) for x in rng.choice(num_qubits, size=2, replace=False))
            getattr(qc, name)(q0, q1)
            operations.append({"opcode": name, "qubits": [q0, q1], "params": []})
        elif kind == "2qp" and num_qubits >= 2:
            name = str(rng.choice(list(PARAM_2Q_GATES.keys())))
            theta = float(rng.uniform(-2 * np.pi, 2 * np.pi))
            q0, q1 = (int(x) for x in rng.choice(num_qubits, size=2, replace=False))
            getattr(qc, name)(theta, q0, q1)
            operations.append({"opcode": name, "qubits": [q0, q1], "params": [theta]})
        elif kind == "3q" and num_qubits >= 3:
            q0, q1, q2 = (int(x) for x in rng.choice(num_qubits, size=3, replace=False))
            qc.ccx(q0, q1, q2)
            operations.append({"opcode": "ccx", "qubits": [q0, q1, q2], "params": []})
        elif kind == "unitary":
            k = int(rng.integers(1, min(MAX_UNITARY_QUBITS, num_qubits) + 1))
            qubits = [int(x) for x in rng.choice(num_qubits, size=k, replace=False)]
            u = random_unitary(rng, k)
            qc.unitary(u, qubits)
            operations.append(
                {
                    "opcode": "unitary",
                    "qubits": qubits,
                    "matrix_real": u.real.tolist(),
                    "matrix_imag": u.imag.tolist(),
                }
            )

    return qc, operations


def random_observable(rng, num_qubits):
    """A random weighted Pauli-string observable (docs/BUILD_PLAN.md
    Phase 5: "tools/gen_fixtures.py extended to emit random SparsePauliOp
    terms alongside each circuit, with Statevector.expectation_value as
    the oracle"). Each term is built via Pauli((z_bits, x_bits)) directly
    from boolean arrays -- no Pauli label string is constructed or
    parsed anywhere in this function, sidestepping any big-endian-label
    convention risk entirely (verified this session, docs/adr/0008:
    Pauli((z,x)) round-trips .x/.z exactly). x_bits[q]/z_bits[q] are
    bit q = qubit q, exactly MENOS's own PauliString convention
    (docs/adr/0006/0007), so the emitted x_qubits/z_qubits lists need no
    translation on the C++ side either.

    Returns (terms_json, SparsePauliOp) -- terms_json is what gets
    written to the fixture file; the SparsePauliOp is used immediately
    to compute the oracle expectation value.
    """
    num_terms = int(rng.integers(MIN_OBSERVABLE_TERMS, MAX_OBSERVABLE_TERMS + 1))
    paulis = []
    coeffs = []
    terms_json = []
    for _ in range(num_terms):
        x_bits = np.zeros(num_qubits, dtype=bool)
        z_bits = np.zeros(num_qubits, dtype=bool)
        x_qubits = []
        z_qubits = []
        for q in range(num_qubits):
            kind = int(rng.integers(0, 4))  # 0=I, 1=X, 2=Y, 3=Z, uniform
            if kind in (1, 2):
                x_bits[q] = True
                x_qubits.append(q)
            if kind in (2, 3):
                z_bits[q] = True
                z_qubits.append(q)
        coeff = float(rng.uniform(-2.0, 2.0))
        paulis.append(Pauli((z_bits, x_bits)))
        coeffs.append(coeff)
        terms_json.append(
            {"x_qubits": x_qubits, "z_qubits": z_qubits, "coeff_real": coeff, "coeff_imag": 0.0}
        )
    return terms_json, SparsePauliOp(paulis, coeffs)


def qasm2_text_if_representable(qc, operations):
    """OpenQASM 2's qelib1.inc has no way to express an arbitrary unitary
    (qiskit.qasm2.dumps emits a custom `gate unitary q0,q1 {...}` block
    for one, confirmed by inspection this session -- exactly the
    "gate block beyond qelib1.inc" construct the Phase 7 parser must
    reject). Circuits containing a "unitary" operation are therefore
    left without a qasm2_text field; the round-trip test asserts the
    parser correctly rejects their dump instead of comparing results.
    """
    if any(op["opcode"] == "unitary" for op in operations):
        return None
    return qasm2.dumps(qc)


def build_circuit_entry(index, num_qubits, qc, operations, rng):
    statevector = Statevector(qc).data
    terms_json, observable = random_observable(rng, num_qubits)
    expected_expectation = Statevector(qc).expectation_value(observable)
    entry = {
        "index": index,
        "num_qubits": num_qubits,
        "global_phase": float(qc.global_phase),
        "operations": operations,
        "expected_statevector_real": statevector.real.tolist(),
        "expected_statevector_imag": statevector.imag.tolist(),
        "observable": {"terms": terms_json},
        "expected_expectation_real": float(np.real(expected_expectation)),
        "expected_expectation_imag": float(np.imag(expected_expectation)),
    }
    qasm2_text = qasm2_text_if_representable(qc, operations)
    if qasm2_text is not None:
        entry["qasm2_text"] = qasm2_text
    return entry


def explicit_y_convention_circuits(start_index):
    """Three hand-picked circuits with a single Y-heavy observable each,
    on 1, 2 and 3 qubits (docs/BUILD_PLAN.md Phase 5: "Explicit cases for
    Y on 1, 2 and 3 qubits confirm the sign convention"). The same three
    circuits already hand-verified this session for the C++
    DenseEngineYConventionTest.* tests, now also exercised through this
    actual fixture pipeline -- two independent confirmations of the same
    values, not one path trusting itself.
    """
    entries = []

    qc1 = QuantumCircuit(1)
    qc1.h(0)
    qc1.s(0)
    ops1 = [{"opcode": "h", "qubits": [0], "params": []},
           {"opcode": "s", "qubits": [0], "params": []}]
    entries.append((qc1, 1, ops1, [{"x_qubits": [0], "z_qubits": [0], "coeff_real": 1.0,
                                    "coeff_imag": 0.0}]))  # <Y> on S*H|0>

    qc2 = QuantumCircuit(2)
    qc2.h(0)
    qc2.cx(0, 1)
    qc2.s(0)
    ops2 = [{"opcode": "h", "qubits": [0], "params": []},
           {"opcode": "cx", "qubits": [0, 1], "params": []},
           {"opcode": "s", "qubits": [0], "params": []}]
    # <YX>: qubit0=X, qubit1=Y (MENOS/qubit-index convention, not a label).
    entries.append((qc2, 2, ops2, [{"x_qubits": [0, 1], "z_qubits": [1], "coeff_real": 1.0,
                                    "coeff_imag": 0.0}]))

    qc3 = QuantumCircuit(3)
    qc3.h(0)
    qc3.s(0)
    qc3.h(1)
    qc3.h(2)
    qc3.s(2)
    ops3 = [{"opcode": "h", "qubits": [0], "params": []},
           {"opcode": "s", "qubits": [0], "params": []},
           {"opcode": "h", "qubits": [1], "params": []},
           {"opcode": "h", "qubits": [2], "params": []},
           {"opcode": "s", "qubits": [2], "params": []}]
    # <YIY>: qubit0=Y, qubit2=Y.
    entries.append((qc3, 3, ops3, [{"x_qubits": [0, 2], "z_qubits": [0, 2], "coeff_real": 1.0,
                                    "coeff_imag": 0.0}]))

    circuits = []
    for offset, (qc, num_qubits, operations, terms_json) in enumerate(entries):
        statevector = Statevector(qc).data
        observable = SparsePauliOp(
            [Pauli((_bits(num_qubits, t["z_qubits"]), _bits(num_qubits, t["x_qubits"])))
             for t in terms_json],
            [complex(t["coeff_real"], t["coeff_imag"]) for t in terms_json],
        )
        expected_expectation = Statevector(qc).expectation_value(observable)
        circuits.append(
            {
                "index": start_index + offset,
                "num_qubits": num_qubits,
                "global_phase": float(qc.global_phase),
                "operations": operations,
                "expected_statevector_real": statevector.real.tolist(),
                "expected_statevector_imag": statevector.imag.tolist(),
                "observable": {"terms": terms_json},
                "expected_expectation_real": float(np.real(expected_expectation)),
                "expected_expectation_imag": float(np.imag(expected_expectation)),
                "qasm2_text": qasm2.dumps(qc),
            }
        )
    return circuits


def _bits(num_qubits, qubits):
    b = np.zeros(num_qubits, dtype=bool)
    for q in qubits:
        b[q] = True
    return b


def main():
    confirm_global_phase_is_tracked()

    rng = np.random.default_rng(SEED)
    circuits = []
    for i in range(NUM_CIRCUITS):
        num_qubits = int(rng.integers(MIN_QUBITS, MAX_QUBITS + 1))
        qc, operations = build_random_circuit(rng, num_qubits)
        circuits.append(build_circuit_entry(i, num_qubits, qc, operations, rng))

    circuits.extend(explicit_y_convention_circuits(len(circuits)))

    with open(OUTPUT_PATH, "w") as f:
        json.dump({"qiskit_version": qiskit.__version__, "circuits": circuits}, f, indent=1)
    print(f"wrote {len(circuits)} circuits to {OUTPUT_PATH}", file=sys.stderr)


if __name__ == "__main__":
    main()
