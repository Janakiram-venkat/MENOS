"""Phase 3 required Python test: factorized backend results match direct
for interleaved components, exercised through the full BackendV2 path
(docs/BUILD_PLAN.md Phase 3, 3.11 "Required Python tests").
"""

from __future__ import annotations

import numpy as np
from qiskit import QuantumCircuit
from qiskit.quantum_info import Statevector

ABS_TOL = 1e-12


def test_interleaved_components_statevector_matches_qiskit_oracle(backend):
    # Global qubits {0,2} and {1,3} form two independent Bell pairs,
    # interleaved -- the same circuit shape docs/BUILD_PLAN.md Phase 3,
    # 3.3 names explicitly. Small enough that MENOS's planner should pick
    # Factorized (verified indirectly: this test only passes if the
    # factorized path is numerically correct end to end through the
    # Qiskit adapter, not just menos._core directly).
    qc = QuantumCircuit(4)
    qc.h(0)
    qc.cx(0, 2)
    qc.h(1)
    qc.cx(1, 3)

    result = backend.run(qc, shots=0, want_statevector=True).result()
    actual = np.asarray(result.get_statevector(qc))
    expected = Statevector(qc).data
    assert np.allclose(actual, expected, atol=ABS_TOL, rtol=0)


def test_interleaved_components_counts_are_correlated_within_each_pair(backend):
    qc = QuantumCircuit(4, 4)
    qc.h(0)
    qc.cx(0, 2)
    qc.h(1)
    qc.cx(1, 3)
    qc.measure(range(4), range(4))

    shots = 2000
    result = backend.run(qc, shots=shots, seed_simulator=1234).result()
    counts = result.get_counts()

    total = 0
    for bitstring, count in counts.items():
        # Qiskit prints the highest clbit first; bitstring[::-1] gives
        # clbit c at index c.
        bits = bitstring[::-1]
        assert bits[0] == bits[2], f"q0/q2 must be correlated: {bitstring}"
        assert bits[1] == bits[3], f"q1/q3 must be correlated: {bitstring}"
        total += count
    assert total == shots


def test_interleaved_components_global_phase_matches_qiskit_oracle(backend):
    # The exact bug caught in this phase's development (docs/adr/0005):
    # global_phase silently dropped when the region containing qubit 0
    # was idle. This circuit's qubit 0 is idle (only qubits {1,2} have
    # gates), directly exercising that regression at the Qiskit adapter
    # level, not just the C++ core.
    qc = QuantumCircuit(3)
    qc.global_phase = np.pi / 3
    qc.h(1)
    qc.cx(1, 2)

    result = backend.run(qc, shots=0, want_statevector=True).result()
    actual = np.asarray(result.get_statevector(qc))
    expected = Statevector(qc).data
    assert np.allclose(actual, expected, atol=ABS_TOL, rtol=0)
