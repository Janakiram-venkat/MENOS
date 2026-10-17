"""docs/adr/0024 gate criterion 5: the parameter-shift gradient of a VQE
fixture equals default.qubit's within 1e-12, and its backprop gradient
within 1e-10. The device has no derivatives of its own: PennyLane runs the
shifted circuits through execute(), each planned alone."""

from __future__ import annotations

import pytest

qml = pytest.importorskip("pennylane")

from pennylane import numpy as pnp  # noqa: E402

from _pennylane_fixtures import TOL  # noqa: E402
from menos.pennylane import MENOSQubitDevice  # noqa: E402

N = 4
# A hand-written Pauli-sum Hamiltonian (no qchem dependency).
HAMILTONIAN = qml.Hamiltonian(
    [-0.81, 0.17, 0.17, -0.22, 0.12, 0.165, 0.045, 0.045, -0.3],
    [
        qml.Identity(0),
        qml.Z(0),
        qml.Z(1),
        qml.Z(2) @ qml.Z(3),
        qml.Z(0) @ qml.Z(1),
        qml.Z(1) @ qml.Z(2),
        qml.X(0) @ qml.X(1) @ qml.Y(2) @ qml.Y(3),
        qml.Y(0) @ qml.Y(1) @ qml.X(2) @ qml.X(3),
        qml.X(3),
    ],
)


def _ansatz(params):
    for layer in range(params.shape[0]):
        for w in range(N):
            qml.RY(params[layer, w, 0], w)
            qml.RZ(params[layer, w, 1], w)
        for w in range(N - 1):
            qml.CNOT([w, w + 1])
        qml.CRX(params[layer, 0, 2], wires=[N - 1, 0])


def _energy(device, diff_method):
    @qml.qnode(device, diff_method=diff_method)
    def energy(params):
        _ansatz(params)
        return qml.expval(HAMILTONIAN)

    return energy


PARAMS = pnp.array(
    pnp.random.default_rng(2026).uniform(-pnp.pi, pnp.pi, size=(2, N, 3)), requires_grad=True
)


def test_energy_matches_default_qubit():
    ours = _energy(MENOSQubitDevice(wires=N), "parameter-shift")(PARAMS)
    ref = _energy(qml.device("default.qubit", wires=N), "parameter-shift")(PARAMS)
    assert abs(ours - ref) < TOL


def test_parameter_shift_gradient_matches_default_qubit():
    ours = qml.grad(_energy(MENOSQubitDevice(wires=N), "parameter-shift"))(PARAMS)
    ref = qml.grad(_energy(qml.device("default.qubit", wires=N), "parameter-shift"))(PARAMS)
    assert pnp.max(pnp.abs(ours - ref)) < TOL


def test_parameter_shift_gradient_matches_backprop():
    ours = qml.grad(_energy(MENOSQubitDevice(wires=N), "parameter-shift"))(PARAMS)
    ref = qml.grad(_energy(qml.device("default.qubit", wires=N), "backprop"))(PARAMS)
    assert pnp.max(pnp.abs(ours - ref)) < 1e-10


def test_best_selects_parameter_shift():
    dev = MENOSQubitDevice(wires=N)
    qnode = _energy(dev, "best")
    with qml.Tracker(dev) as tracker:
        qml.grad(qnode)(PARAMS)
    # Parameter-shift: two shifted circuits per trainable parameter, run as
    # batches through execute().
    assert tracker.totals["executions"] > 2 * PARAMS.size
