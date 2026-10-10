"""Criterion 8: multiple quantum/classical registers, unequal register
sizes and nontrivial measurement mapping (docs/BUILD_PLAN.md Phase 2, 2.8
item 8). Exercises circuit.find_bit()-based global indexing in convert.py
and result_adapter.py.
"""

from __future__ import annotations

import numpy as np
from qiskit import ClassicalRegister, QuantumCircuit, QuantumRegister
from qiskit.quantum_info import Statevector

ABS_TOL = 1e-12


def test_two_quantum_registers_unequal_sizes_statevector(backend):
    qa = QuantumRegister(1, "a")
    qb = QuantumRegister(2, "b")
    qc = QuantumCircuit(qa, qb)
    qc.h(qa[0])
    qc.cx(qa[0], qb[0])
    qc.x(qb[1])

    result = backend.run(qc, shots=0, want_statevector=True).result()
    actual = np.asarray(result.get_statevector(qc))
    expected = Statevector(qc).data
    assert np.allclose(actual, expected, atol=ABS_TOL, rtol=0)


def test_multi_register_measurement_mapping(backend):
    qr = QuantumRegister(3, "q")
    ca = ClassicalRegister(1, "a")
    cb = ClassicalRegister(2, "b")
    qc = QuantumCircuit(qr, ca, cb)
    qc.x(qr[0])  # qubit 0 -> 1
    qc.x(qr[2])  # qubit 2 -> 1, qubit 1 stays 0
    # Nontrivial mapping: qubit 2 -> register 'a' bit 0, qubit 0 -> register
    # 'b' bit 1, qubit 1 -> register 'b' bit 0.
    qc.measure(qr[2], ca[0])
    qc.measure(qr[0], cb[1])
    qc.measure(qr[1], cb[0])

    result = backend.run(qc, shots=10, seed_simulator=1).result()
    counts = result.get_counts()
    # Verified independently against qiskit.result.Result.from_dict with a
    # hand-built header (not assumed): Qiskit displays the
    # later-declared/higher-clbit-index register first, space-separated,
    # each register's own highest bit first -- "10 1" here (b1=1,b0=0,
    # then a0=1), not the declaration order "a b".
    assert counts == {"10 1": 10}
