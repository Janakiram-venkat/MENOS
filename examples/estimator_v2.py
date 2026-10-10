"""The EstimatorV2 primitive: several pubs, and parameters bound up front."""

import numpy as np
from qiskit import QuantumCircuit
from qiskit.circuit import Parameter
from qiskit.quantum_info import SparsePauliOp, Statevector

from menos.qiskit import MENOSEstimator

theta = Parameter("theta")
template = QuantumCircuit(1)
template.ry(theta, 0)

estimator = MENOSEstimator()
z = SparsePauliOp("Z")

# MENOSEstimator rejects circuits that still carry parameters, so each pub
# binds them first (QuantumCircuit.assign_parameters).
angles = [0.0, np.pi / 3, np.pi / 2, np.pi]
pubs = [(template.assign_parameters({theta: angle}), z) for angle in angles]
results = estimator.run(pubs).result()

for angle, pub_result in zip(angles, results):
    value = float(pub_result.data.evs)
    print(f"theta = {angle:.4f}   <Z> = {value:.6f}")
    assert abs(value - np.cos(angle)) < 1e-12  # <Z> after RY(theta) is cos(theta)
    assert pub_result.metadata["target_precision"] == 0.0  # exact engines: no sampling noise

qc = template.assign_parameters({theta: 1.1})
assert abs(float(estimator.run([(qc, z)]).result()[0].data.evs) - Statevector(qc).expectation_value(z).real) < 1e-12
