"""Exact statevectors, checked against Qiskit's own reference simulator."""

import numpy as np
from qiskit import QuantumCircuit
from qiskit.quantum_info import Statevector

from menos.qiskit import MENOSBackend

qc = QuantumCircuit(3)
qc.h(0)
qc.cx(0, 1)
qc.rx(0.7, 2)
qc.cp(0.4, 1, 2)
qc.t(0)

backend = MENOSBackend()
state = np.asarray(backend.run(qc, shots=0, want_statevector=True).result().get_statevector())
reference = Statevector(qc).data

print("amplitudes:", np.round(state, 6))
assert np.max(np.abs(state - reference)) < 1e-12
print("max |difference| from Qiskit's Statevector:", float(np.max(np.abs(state - reference))))
