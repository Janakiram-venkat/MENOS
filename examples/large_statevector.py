"""A larger exact statevector: 20 qubits, about 16 MiB of amplitudes."""

import math
import random

import numpy as np
from qiskit import QuantumCircuit

from menos.qiskit import MENOSBackend

n = 20
rng = random.Random(3)
qc = QuantumCircuit(n)
for _ in range(3):
    for q in range(n):
        qc.ry(rng.uniform(0, math.pi), q)
    for q in range(0, n - 1, 2):
        qc.cx(q, q + 1)
    for q in range(1, n - 1, 2):
        qc.cx(q, q + 1)

state = np.asarray(
    MENOSBackend().run(qc, shots=0, want_statevector=True).result().get_statevector()
)
print("amplitudes:", state.size, "| bytes:", state.nbytes)
assert state.size == 2**n
assert abs(np.linalg.norm(state) - 1.0) < 1e-12  # the state is normalised
