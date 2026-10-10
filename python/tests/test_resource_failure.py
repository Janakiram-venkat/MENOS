"""Criterion 14: a circuit rejected by the Phase 1 memory budget fails
before native allocation and is visible through the Python API
(docs/BUILD_PLAN.md Phase 2, 2.8 item 14).

Goes through the full Qiskit adapter path (MENOSBackend.run), not just
menos._core directly (test_error_mapping.py covers that boundary).
"""

from __future__ import annotations

from qiskit import QuantumCircuit

from menos.exceptions import ResourceExceededError


def test_too_many_qubits_for_default_budget_fails_through_backend(backend):
    # 55 qubits: 16 * 2^55 bytes (~576 PiB) fits in uint64_t (unlike, say,
    # 60 qubits, where 16*2^60 overflows uint64_t itself and checked_mul
    # throws first -- a different, also-legitimate ResourceExceeded path,
    # just not the budget-comparison one this test targets) and vastly
    # exceeds any real machine's default (50%-of-total-memory) budget
    # (src/plan/hardware_profile.h).
    qc = QuantumCircuit(55)
    for q in range(55):
        qc.h(q)

    try:
        backend.run(qc, shots=0, want_statevector=True)
    except ResourceExceededError as exc:
        assert "bytes" in str(exc)
    else:
        raise AssertionError("expected ResourceExceededError for a 60-qubit statevector request")
