"""Criterion 2 (Python half): a 500-qubit Clifford circuit runs through
MENOSBackend via the stabilizer engine with correct count keys, and the
same circuit with a statevector request fails with ResourceExceededError
naming the dense engine (docs/BUILD_PLAN.md Phase 4). The Phase 2 Target
advertises num_qubits=None (no cap) and convert.py has no qubit-count
limit of its own, so no adapter code change was needed for this -- this
test is the coverage the spec asks for in that case.
"""

from __future__ import annotations

from qiskit import QuantumCircuit

from menos.exceptions import ResourceExceededError


def make_500_qubit_clifford_circuit() -> QuantumCircuit:
    n = 500
    qc = QuantumCircuit(n, n)
    for q in range(n):
        qc.h(q)
    for q in range(n - 1):
        qc.cx(q, q + 1)
    for q in range(n):
        qc.measure(q, q)
    return qc


def test_five_hundred_qubit_clifford_circuit_runs_via_stabilizer_engine(backend):
    qc = make_500_qubit_clifford_circuit()
    result = backend.run(qc, shots=5, seed_simulator=1).result()
    counts = result.get_counts()
    assert sum(counts.values()) == 5
    for key in counts:
        assert len(key) == 500


def test_five_hundred_qubit_statevector_request_fails_naming_dense(backend):
    qc = make_500_qubit_clifford_circuit()
    try:
        backend.run(qc, shots=0, want_statevector=True)
    except ResourceExceededError as exc:
        assert "only the dense engine can produce a statevector" in str(exc)
    else:
        raise AssertionError("expected ResourceExceededError for a 500-qubit statevector request")
