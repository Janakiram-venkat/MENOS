"""Criterion 2: MENOSBackend can be constructed and exposes the required
installed BackendV2 surface (docs/BUILD_PLAN.md Phase 2, 2.10 item 2)."""

from __future__ import annotations

from qiskit import QuantumCircuit
from qiskit.providers import BackendV2, Options
from qiskit.transpiler import Target


def test_is_a_backendv2(backend):
    assert isinstance(backend, BackendV2)
    assert backend.version == 2


def test_target_property(backend):
    assert isinstance(backend.target, Target)


def test_max_circuits_is_one(backend):
    assert backend.max_circuits == 1


def test_default_options(backend):
    assert isinstance(backend.options, Options)
    assert backend.options.shots == 1024
    assert backend.options.seed_simulator is None
    assert backend.options.want_statevector is False
    assert backend.options.noise_model is None  # Phase 12, docs/adr/0016


def test_run_with_no_options_works(backend):
    qc = QuantumCircuit(1, 1)
    qc.x(0)
    qc.measure(0, 0)
    result = backend.run(qc).result()
    counts = result.get_counts()
    assert counts == {"1": 1024}


def test_run_rejects_multiple_circuits(backend):
    qc = QuantumCircuit(1, 1)
    qc.measure(0, 0)
    try:
        backend.run([qc, qc])
    except ValueError as exc:
        assert "batching" in str(exc)
    else:
        raise AssertionError("expected ValueError for a multi-circuit submission")


def test_run_rejects_unknown_option(backend):
    qc = QuantumCircuit(1, 1)
    qc.measure(0, 0)
    try:
        backend.run(qc, not_a_real_option=True)
    except AttributeError:
        pass
    else:
        raise AssertionError("expected AttributeError for an unknown option")
