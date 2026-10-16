"""docs/adr/0024 gate criterion 7: every construct of section 2.2 is refused
with MENOSDeviceUnsupportedError (both menos.UnsupportedFeatureError and
PennyLane's DeviceError, Q9), naming the construct."""

from __future__ import annotations

import pytest

qml = pytest.importorskip("pennylane")

import numpy as np  # noqa: E402
from pennylane.exceptions import DeviceError, QuantumFunctionError  # noqa: E402

import menos  # noqa: E402
from menos.pennylane import MENOSDeviceUnsupportedError, MENOSQubitDevice  # noqa: E402


def _qnode(body, shots=None, wires=2, **kwargs):
    node = qml.qnode(MENOSQubitDevice(wires=wires), **kwargs)(body)
    return qml.set_shots(node, shots=shots) if shots else node


def _mcm():
    qml.measure(0)
    return qml.expval(qml.Z(1))


def _cond():
    m = qml.measure(0)
    qml.cond(m, qml.PauliX)(1)
    return qml.expval(qml.Z(1))


def _reset():
    qml.measure(0, reset=True)
    return qml.expval(qml.Z(0))


def _measure_value():
    m = qml.measure(0)
    return qml.expval(m)


REFUSED = {
    "mid-circuit measurement": (_mcm, None, "MidMeasure"),
    "qml.cond": (_cond, None, "MidMeasure"),
    "reset": (_reset, None, "MidMeasure"),
    "expval of a measurement value": (_measure_value, None, "MidMeasure"),
    "Hermitian": (lambda: qml.expval(qml.Hermitian(np.diag([1.0, -1.0]), 0)), None, "Hermitian"),
    "Projector": (lambda: qml.expval(qml.Projector([1], wires=0)), None, "Projector"),
    "SparseHamiltonian": (
        lambda: qml.expval(qml.SparseHamiltonian(qml.Hamiltonian([1.0], [qml.Z(0)]).sparse_matrix(), wires=[0])),
        None,
        "SparseHamiltonian",
    ),
    "Hadamard observable": (lambda: qml.expval(qml.Hadamard(0)), None, "Hadamard"),
    "non-Pauli factor of a product": (
        lambda: qml.expval(qml.Z(0) @ qml.Hadamard(1)), None, r"observable Hadamard \(no Pauli"
    ),
    "density_matrix": (lambda: qml.density_matrix(wires=[0]), None, "DensityMatrixMP"),
    "mutual_info": (lambda: qml.mutual_info(wires0=[0], wires1=[1]), None, "MutualInfoMP"),
    "vn_entropy": (lambda: qml.vn_entropy(wires=[0]), None, "VnEntropyMP"),
    "purity": (lambda: qml.purity(wires=[0]), None, "PurityMP"),
    "classical_shadow": (lambda: qml.classical_shadow(wires=[0, 1]), 10, "ClassicalShadowMP"),
    "shadow_expval": (lambda: qml.shadow_expval(qml.Z(0)), 10, "ShadowExpvalMP"),
    "state with shots": (lambda: qml.state(), 10, "StateMP"),
    "probs of a non-Pauli observable": (
        lambda: qml.probs(op=qml.Hermitian(np.diag([1.0, -1.0]), 0)), None, "Hermitian"
    ),
    "Snapshot": (lambda: (qml.Snapshot(), qml.expval(qml.Z(0)))[1], None, "Snapshot"),
    "sample with shots=None": (lambda: qml.sample(), None, "SampleMP"),
    "counts with shots=None": (lambda: qml.counts(), None, "CountsMP"),
}


@pytest.mark.parametrize("name", sorted(REFUSED))
def test_refused_by_name(name):
    body, shots, needle = REFUSED[name]

    def circuit():
        qml.Hadamard(0)
        return body()

    with pytest.raises(MENOSDeviceUnsupportedError, match=needle) as info:
        _qnode(circuit, shots=shots)()
    assert isinstance(info.value, menos.UnsupportedFeatureError)
    assert isinstance(info.value, DeviceError)
    assert "menos.qubit" in str(info.value)


class _NoMatrixNoDecomposition(qml.operation.Operation):
    num_wires = 1


def test_undecomposable_operation_is_refused_by_name():
    def circuit():
        _NoMatrixNoDecomposition(wires=0)
        return qml.expval(qml.Z(0))

    with pytest.raises(MENOSDeviceUnsupportedError, match="_NoMatrixNoDecomposition"):
        _qnode(circuit)()


@pytest.mark.parametrize("diff_method", ["backprop", "adjoint"])
def test_device_gradients_are_refused(diff_method):
    # The device has no derivatives of its own (adjoint is Phase 23);
    # PennyLane itself refuses, naming the device.
    def circuit(x):
        qml.RX(x, 0)
        return qml.expval(qml.Z(0))

    with pytest.raises(QuantumFunctionError, match=f"menos.qubit.*does not support {diff_method}"):
        node = _qnode(circuit, diff_method=diff_method)
        qml.grad(node)(qml.numpy.array(0.3, requires_grad=True))
