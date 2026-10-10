"""Criterion 9: supported matrix-bearing instructions become KIR Unitary
and match the oracle (docs/BUILD_PLAN.md Phase 2, 2.8 item 9)."""

from __future__ import annotations

import numpy as np
from qiskit import QuantumCircuit
from qiskit.quantum_info import Statevector

ABS_TOL = 1e-12


def test_custom_unitary_matches_oracle(backend):
    # An explicit, non-standard 2-qubit unitary (not decomposed into named
    # gates by Qiskit): a permutation-plus-phase matrix.
    matrix = np.array(
        [
            [0, 1, 0, 0],
            [0, 0, 1j, 0],
            [0, 0, 0, -1],
            [1, 0, 0, 0],
        ],
        dtype=complex,
    )
    qc = QuantumCircuit(2)
    qc.h(0)
    qc.unitary(matrix, [0, 1], label="custom")

    result = backend.run(qc, shots=0, want_statevector=True).result()
    actual = np.asarray(result.get_statevector(qc))
    expected = Statevector(qc).data
    assert np.allclose(actual, expected, atol=ABS_TOL, rtol=0)


def test_custom_unitary_exceeding_kmaxunitaryqubits_is_rejected(backend):
    from menos._core import kMaxUnitaryQubits
    from menos.exceptions import UnsupportedFeatureError

    k = kMaxUnitaryQubits + 1
    matrix = np.eye(2**k, dtype=complex)
    qc = QuantumCircuit(k)
    qc.unitary(matrix, list(range(k)))

    try:
        backend.run(qc)
    except UnsupportedFeatureError:
        pass
    else:
        raise AssertionError(
            f"expected UnsupportedFeatureError for a {k}-qubit Unitary (kMaxUnitaryQubits="
            f"{kMaxUnitaryQubits})"
        )
