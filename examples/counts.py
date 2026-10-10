"""Counts: seeds, reproducibility and partial measurement."""

from qiskit import QuantumCircuit

from menos.qiskit import MENOSBackend

backend = MENOSBackend()

qc = QuantumCircuit(3, 3)
qc.h(0)
qc.h(1)
qc.h(2)
qc.measure([0, 1, 2], [0, 1, 2])

# The same circuit, options and seed always give the same counts.
first = backend.run(qc, shots=500, seed_simulator=42).result().get_counts()
again = backend.run(qc, shots=500, seed_simulator=42).result().get_counts()
other = backend.run(qc, shots=500, seed_simulator=43).result().get_counts()
assert first == again
assert first != other
print("seed 42:", dict(sorted(first.items())))
print("seed 43:", dict(sorted(other.items())))

# Measuring a subset of the qubits: only the measured classical bits appear.
partial = QuantumCircuit(3, 1)
partial.x(2)
partial.measure(2, 0)
only = backend.run(partial, shots=100, seed_simulator=1).result().get_counts()
print("one measured qubit:", only)
assert only == {"1": 100}
