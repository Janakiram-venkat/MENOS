"""docs/adr/0024 gate criterion 3: analytic state/probs/expval/var equal
default.qubit's within 1e-12, global phase included; the exactness guard
(Q3) and the identity term (section 3.4)."""

from __future__ import annotations

import pytest

qml = pytest.importorskip("pennylane")

import numpy as np  # noqa: E402

import menos  # noqa: E402
from _pennylane_fixtures import (  # noqa: E402
    TOL,
    fixture_observable,
    fixture_operations,
    fixture_statevector_big_endian,
    load_fixtures,
    run_both,
)
from menos.pennylane import MENOSQubitDevice  # noqa: E402

FIXTURES = load_fixtures()


def _close(a, b):
    np.testing.assert_allclose(np.asarray(a), np.asarray(b), rtol=0, atol=TOL)


@pytest.mark.parametrize("entry", FIXTURES, ids=lambda e: f"circuit{e['index']}")
def test_random_fixture_matches_default_qubit(entry):
    n = entry["num_qubits"]
    ops = fixture_operations(entry)
    obs = fixture_observable(entry)
    probs_wires = list(range(n))[::2][::-1]  # a reordered subset
    measurements = [qml.state(), qml.probs(wires=probs_wires), qml.expval(obs), qml.var(obs)]
    ref = qml.device("default.qubit", wires=n)
    dev = MENOSQubitDevice(wires=n)
    expected, actual = run_both(ref, dev, ops, measurements)

    # The PennyLane rebuild is the fixture's circuit (Qiskit oracle).
    _close(expected[0], fixture_statevector_big_endian(entry))
    assert abs(expected[2] - entry["expected_expectation_real"]) < TOL

    for e, a in zip(expected, actual):
        _close(a, e)


_TEMPLATE_CASES = {
    "QFT": lambda: [qml.Hadamard(1), qml.QFT(wires=range(4))],
    "StronglyEntanglingLayers": lambda: [
        qml.StronglyEntanglingLayers(
            np.random.default_rng(3).uniform(-np.pi, np.pi, size=(2, 4, 3)), wires=range(4)
        )
    ],
    "BasicEntanglerLayers": lambda: [
        qml.BasicEntanglerLayers([[0.1, -0.7, 1.3, 0.4]], wires=range(4), rotation=qml.RY)
    ],
    "BasisState": lambda: [qml.BasisState(np.array([1, 0, 1, 1]), wires=range(4)), qml.Hadamard(0)],
    "StatePrep": lambda: [
        qml.StatePrep(np.arange(1, 17) / np.linalg.norm(np.arange(1, 17)), wires=range(4)),
        qml.CNOT([0, 3]),
    ],
    # Six wires: above kMaxUnitaryQubits, so PennyLane decomposes it.
    "MultiControlledX": lambda: [
        *[qml.Hadamard(w) for w in range(5)],
        qml.MultiControlledX(wires=range(6)),
    ],
    "ctrl(RY)": lambda: [qml.Hadamard(0), qml.ctrl(qml.RY(0.4, wires=2), control=[0, 1])],
    "GlobalPhase under ctrl": lambda: [
        qml.Hadamard(0),
        qml.ctrl(qml.GlobalPhase(0.9, wires=1), control=0),
    ],
    "Rot and CRot": lambda: [qml.Rot(0.1, 0.2, 0.3, wires=0), qml.CRot(0.4, 0.5, 0.6, wires=[0, 1])],
    "IsingXX and SISWAP": lambda: [qml.Hadamard(0), qml.IsingXX(0.3, wires=[0, 1]), qml.SISWAP([1, 2])],
}


@pytest.mark.parametrize("name", sorted(_TEMPLATE_CASES))
def test_decomposed_and_unmapped_operations_match_default_qubit(name):
    ops = _TEMPLATE_CASES[name]()
    wires = qml.wires.Wires.all_wires([op.wires for op in ops])
    obs = qml.X(wires[0]) @ qml.Z(wires[-1]) + 0.4 * qml.Y(wires[0])
    measurements = [qml.state(), qml.probs(wires=wires[::-1]), qml.expval(obs), qml.var(obs)]
    expected, actual = run_both(
        qml.device("default.qubit", wires=wires), MENOSQubitDevice(wires=wires), ops, measurements
    )
    for e, a in zip(expected, actual):
        _close(a, e)


@pytest.mark.parametrize(
    "obs",
    [lambda: qml.X(0), lambda: qml.Y(1), lambda: qml.Y(0) @ qml.X(2), lambda: qml.Z(2) @ qml.Y(0)],
    ids=["X0", "Y1", "Y0X2", "Z2Y0"],
)
def test_probs_of_an_observable_match_default_qubit(obs):
    ops = [qml.RX(0.4, 0), qml.Hadamard(1), qml.CNOT([1, 2]), qml.RY(-0.9, 2)]
    measurements = [qml.probs(op=obs()), qml.state(), qml.probs(wires=[2, 1])]
    expected, actual = run_both(
        qml.device("default.qubit", wires=3), MENOSQubitDevice(wires=3), ops, measurements
    )
    for e, a in zip(expected, actual):
        _close(a, e)


def test_string_wire_labels_without_device_wires():
    ops = [qml.Hadamard("anc"), qml.CNOT(["anc", "data"]), qml.RX(0.3, "z")]
    measurements = [qml.state(), qml.expval(qml.Z("anc") @ qml.Z("data") + qml.X("z"))]
    expected, actual = run_both(qml.device("default.qubit"), MENOSQubitDevice(), ops, measurements)
    for e, a in zip(expected, actual):
        _close(a, e)


def test_parameter_broadcasting():
    @qml.qnode(MENOSQubitDevice(wires=2))
    def circuit(x):
        qml.RX(x, 0)
        qml.CNOT([0, 1])
        return qml.expval(qml.Z(1)), qml.probs(wires=[1, 0])

    @qml.qnode(qml.device("default.qubit", wires=2))
    def reference(x):
        qml.RX(x, 0)
        qml.CNOT([0, 1])
        return qml.expval(qml.Z(1)), qml.probs(wires=[1, 0])

    x = np.array([0.1, 0.7, 2.3])
    for e, a in zip(reference(x), circuit(x)):
        _close(a, e)


def test_identity_terms_are_added_exactly():
    dev = MENOSQubitDevice(wires=2)
    ops = [qml.RY(0.8, 0)]
    # Identity only: no MENOS observable is built at all.
    tape = qml.tape.QuantumScript(ops, [qml.expval(2.5 * qml.Identity(0))])
    assert abs(qml.execute([tape], dev)[0] - 2.5) < TOL
    tape = qml.tape.QuantumScript(ops, [qml.var(qml.Identity(1))])
    assert abs(qml.execute([tape], dev)[0]) < TOL
    # Identity plus a Pauli term.
    tape = qml.tape.QuantumScript(ops, [qml.expval(qml.Z(0) + 0.5 * qml.Identity(1))])
    assert abs(qml.execute([tape], dev)[0] - (np.cos(0.8) + 0.5)) < TOL


def test_single_pauli_variance():
    dev = MENOSQubitDevice(wires=1)
    tape = qml.tape.QuantumScript([qml.RX(0.6, 0)], [qml.var(qml.Z(0))])
    assert abs(qml.execute([tape], dev)[0] - (1 - np.cos(0.6) ** 2)) < TOL


class _NonExact:
    exact = False
    expectation_standard_error = 0.01
    expectation_value = 0.5 + 0j
    statevector = None


def test_non_exact_analytic_result_is_refused(monkeypatch):
    import menos.pennylane.device as device_module

    monkeypatch.setattr(device_module, "menos_run", lambda circuit, options: _NonExact())
    dev = MENOSQubitDevice(wires=1)
    tape = qml.tape.QuantumScript([qml.RX(0.6, 0)], [qml.expval(qml.Z(0))])
    with pytest.raises(menos.InternalError, match="non-exact"):
        qml.execute([tape], dev)


def test_options_reach_menos():
    dev = MENOSQubitDevice(wires=1, backend="cpu", time_budget_seconds=60.0)
    tape = qml.tape.QuantumScript([qml.RX(0.6, 0)], [qml.expval(qml.Z(0))])
    assert abs(qml.execute([tape], dev)[0] - np.cos(0.6)) < TOL
    with pytest.raises(menos.InvalidCircuitError):
        qml.execute([tape], MENOSQubitDevice(wires=1, time_budget_seconds=-1.0))


@pytest.mark.parametrize(
    "kwargs, error",
    [
        ({"seed": 1.5}, TypeError),
        ({"backend": "gpu"}, ValueError),
        ({"noise_method": "trajectories"}, ValueError),
    ],
)
def test_constructor_validation(kwargs, error):
    with pytest.raises(error):
        MENOSQubitDevice(wires=1, **kwargs)
