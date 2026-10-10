"""Criterion 5: circuits differing only by global phase produce
correspondingly different statevectors, matching Qiskit
(docs/BUILD_PLAN.md Phase 2, 2.8 item 5)."""

from __future__ import annotations

import cmath

import numpy as np
from qiskit import QuantumCircuit
from qiskit.quantum_info import Statevector

ABS_TOL = 1e-12


def test_global_phase_matches_qiskit(backend):
    qc = QuantumCircuit(1)
    qc.h(0)
    qc.global_phase = np.pi / 3

    result = backend.run(qc, shots=0, want_statevector=True).result()
    actual = np.asarray(result.get_statevector(qc))
    expected = Statevector(qc).data

    assert np.allclose(actual, expected, atol=ABS_TOL, rtol=0)


def test_rz_two_pi_is_a_pure_global_phase_flip(backend):
    # RZ(2*pi) = -I: CLAUDE.md's "RZ is not P" domain invariant, exercised
    # end-to-end through the Qiskit adapter.
    qc = QuantumCircuit(1)
    qc.rz(2 * np.pi, 0)

    result = backend.run(qc, shots=0, want_statevector=True).result()
    actual = np.asarray(result.get_statevector(qc))

    assert np.allclose(actual, [-1.0, 0.0], atol=ABS_TOL, rtol=0)


def test_differing_global_phase_gives_differing_statevector(backend):
    qc_a = QuantumCircuit(1)
    qc_a.x(0)
    qc_a.global_phase = 0.0

    qc_b = QuantumCircuit(1)
    qc_b.x(0)
    qc_b.global_phase = np.pi / 5

    sv_a = np.asarray(backend.run(qc_a, shots=0, want_statevector=True).result().get_statevector(qc_a))
    sv_b = np.asarray(backend.run(qc_b, shots=0, want_statevector=True).result().get_statevector(qc_b))

    phase = cmath.exp(1j * np.pi / 5)
    assert np.allclose(sv_b, sv_a * phase, atol=ABS_TOL, rtol=0)
    assert not np.allclose(sv_a, sv_b, atol=ABS_TOL, rtol=0)
