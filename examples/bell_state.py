"""Bell state through the Qiskit BackendV2 adapter."""

from qiskit import QuantumCircuit

from menos.qiskit import MENOSBackend

qc = QuantumCircuit(2, 2)
qc.h(0)
qc.cx(0, 1)
qc.measure([0, 1], [0, 1])

backend = MENOSBackend()
result = backend.run(qc, shots=1000, seed_simulator=7).result()
counts = result.get_counts()
print(counts)

# A Bell pair only ever yields 00 or 11, and every shot is accounted for.
assert set(counts) <= {"00", "11"}
assert sum(counts.values()) == 1000
