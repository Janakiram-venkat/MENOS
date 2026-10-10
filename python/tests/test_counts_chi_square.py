"""Criterion 6: seeded counts match the exact probability distribution
under a statistically justified chi-square test (docs/BUILD_PLAN.md
Phase 2, 2.8 item 6).

The threshold is the 0.001-significance critical value of the chi-square
distribution with 1 degree of freedom (2 outcomes), computed from first
principles (regularized incomplete gamma series) -- the same method
already used and verified in tests/engines/dense/sample_test.cpp (C++
Phase 1), not tuned until this test passed.
"""

from __future__ import annotations

import numpy as np
from qiskit import QuantumCircuit

CHI_SQUARE_CRITICAL_DF1_P0001 = 10.8276


def test_counts_chi_square(backend):
    p0 = 0.3
    theta = 2 * np.arccos(np.sqrt(p0))  # P(qubit=0) = cos^2(theta/2) = p0

    qc = QuantumCircuit(1, 1)
    qc.ry(theta, 0)
    qc.measure(0, 0)

    shots = 100_000
    result = backend.run(qc, shots=shots, seed_simulator=31415).result()
    counts = result.get_counts()

    expected_0 = p0 * shots
    expected_1 = (1 - p0) * shots
    observed_0 = counts.get("0", 0)
    observed_1 = counts.get("1", 0)

    chi_square = (observed_0 - expected_0) ** 2 / expected_0 + (
        observed_1 - expected_1
    ) ** 2 / expected_1

    assert chi_square < CHI_SQUARE_CRITICAL_DF1_P0001, (
        f"chi_square={chi_square} (would reject the null hypothesis that counts match "
        f"the exact Born-rule probabilities); counts={counts}"
    )
