"""PennyLane device menos.qubit: a small variational energy, its
parameter-shift gradient, and sampled counts, each checked against
PennyLane's own default.qubit.

Needs `pip install "menos[pennylane]"` (CPython 3.11 or later).
"""

import numpy as np
import pennylane as qml
from pennylane import numpy as pnp

dev = qml.device("menos.qubit", wires=3, seed=7)
ref = qml.device("default.qubit", wires=3)

H = qml.Hamiltonian(
    [0.5, -0.8, 0.3, 0.2],
    [qml.Z(0) @ qml.Z(1), qml.X(1), qml.Y(0) @ qml.Y(2), qml.Identity(0)],
)


def ansatz(params):
    for w in range(3):
        qml.RY(params[w], w)
    qml.CNOT([0, 1])
    qml.CNOT([1, 2])
    qml.CRX(params[3], wires=[2, 0])


@qml.qnode(dev, diff_method="parameter-shift")
def energy(params):
    ansatz(params)
    return qml.expval(H)


@qml.qnode(ref, diff_method="parameter-shift")
def energy_ref(params):
    ansatz(params)
    return qml.expval(H)


params = pnp.array([0.1, -0.4, 0.9, 0.3], requires_grad=True)
e, g = energy(params), qml.grad(energy)(params)
print("energy:", round(float(e), 6))
print("gradient:", np.round(g, 6))
assert abs(e - energy_ref(params)) < 1e-12
assert np.max(np.abs(g - qml.grad(energy_ref)(params))) < 1e-12


@qml.set_shots(2000)
@qml.qnode(dev)
def bell_counts():
    qml.Hadamard(0)
    qml.CNOT([0, 1])
    return qml.counts(wires=[0, 1])


counts = {str(k): int(v) for k, v in bell_counts().items()}
print("counts:", counts)
assert set(counts) <= {"00", "11"} and sum(counts.values()) == 2000
