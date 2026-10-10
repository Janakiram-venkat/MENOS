"""Random circuits, transpiled for the backend and checked against Qiskit."""

import numpy as np
from qiskit import transpile
from qiskit.circuit.random import random_circuit
from qiskit.quantum_info import Statevector

from menos.qiskit import MENOSBackend

backend = MENOSBackend()
worst = 0.0

for seed in range(5):
    qc = random_circuit(6, 6, max_operands=3, seed=seed)
    # random_circuit may emit gates outside MENOS's native set; the backend's
    # Target tells the transpiler exactly which gates to lower to.
    lowered = transpile(qc, backend, seed_transpiler=0)
    state = np.asarray(
        backend.run(lowered, shots=0, want_statevector=True).result().get_statevector()
    )
    # Global phase is part of the state in MENOS (and in Qiskit's Statevector).
    reference = Statevector(lowered).data
    difference = float(np.max(np.abs(state - reference)))
    worst = max(worst, difference)
    print(f"seed {seed}: max |difference| = {difference:.2e}")

assert worst < 1e-10
