"""Criterion 10: reset, mid-circuit measurement, classical control and
unbound parameters fail with the intended exception and a useful message
(docs/BUILD_PLAN.md Phase 2, 2.8 item 10).

Noise is supported from Phase 12 through Qiskit Aer noise models, and
unverified noise types are rejected; both are covered by
test_noise_backend.py (docs/adr/0016).
"""

from __future__ import annotations

from qiskit import QuantumCircuit
from qiskit.circuit import Parameter

from menos.exceptions import InvalidCircuitError, UnsupportedFeatureError


def test_reset_is_rejected(backend):
    qc = QuantumCircuit(1, 1)
    qc.reset(0)
    qc.measure(0, 0)
    try:
        backend.run(qc)
    except UnsupportedFeatureError as exc:
        assert "reset" in str(exc)
    else:
        raise AssertionError("expected UnsupportedFeatureError for reset")


def test_mid_circuit_measurement_gate_after_measure_is_rejected(backend):
    # Terminal-measurement-only: a measurement must come after the last
    # non-measurement operation on its qubit (CLAUDE.md domain invariant).
    qc = QuantumCircuit(1, 2)
    qc.measure(0, 0)
    qc.x(0)
    qc.measure(0, 1)
    try:
        backend.run(qc, shots=10)
    except UnsupportedFeatureError:
        pass
    else:
        raise AssertionError("expected UnsupportedFeatureError for non-terminal measurement")


def test_classical_control_if_else_is_rejected(backend):
    qc = QuantumCircuit(1, 1)
    qc.h(0)
    qc.measure(0, 0)
    with qc.if_test((qc.cregs[0], 1)):
        qc.x(0)
    try:
        backend.run(qc, shots=10)
    except UnsupportedFeatureError as exc:
        assert "if_else" in str(exc)
    else:
        raise AssertionError("expected UnsupportedFeatureError for if_else")


def test_unbound_parameter_is_rejected(backend):
    theta = Parameter("theta")
    qc = QuantumCircuit(1)
    qc.rx(theta, 0)
    try:
        backend.run(qc, shots=0, want_statevector=True)
    except InvalidCircuitError as exc:
        assert "unbound" in str(exc)
    else:
        raise AssertionError("expected InvalidCircuitError for an unbound parameter")


def test_bound_parameter_is_accepted(backend):
    theta = Parameter("theta")
    qc = QuantumCircuit(1)
    qc.rx(theta, 0)
    bound = qc.assign_parameters({theta: 1.23})
    # Should not raise.
    backend.run(bound, shots=0, want_statevector=True).result()
