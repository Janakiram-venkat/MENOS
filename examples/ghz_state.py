"""A five-qubit GHZ state: counts, and the exact statevector."""

import numpy as np
from qiskit import QuantumCircuit

from menos.qiskit import MENOSBackend

n = 5
backend = MENOSBackend()

# Counts: measure every qubit.
measured = QuantumCircuit(n, n)
measured.h(0)
for q in range(n - 1):
    measured.cx(q, q + 1)
measured.measure(range(n), range(n))
counts = backend.run(measured, shots=2000, seed_simulator=11).result().get_counts()
print(counts)
assert set(counts) <= {"0" * n, "1" * n}
assert sum(counts.values()) == 2000

# Statevector: the same circuit without measurements.
unmeasured = QuantumCircuit(n)
unmeasured.h(0)
for q in range(n - 1):
    unmeasured.cx(q, q + 1)
state = backend.run(unmeasured, shots=0, want_statevector=True).result().get_statevector()
expected = np.zeros(2**n, dtype=complex)
expected[0] = expected[-1] = 1 / np.sqrt(2)
assert np.allclose(np.asarray(state), expected, atol=1e-12)
print("statevector matches the analytic GHZ state")
