"""Circuit cutting: an expectation value recombined from two smaller fragments.

Two four-qubit clusters are coupled through one qubit. MENOS cuts that wire,
simulates each fragment separately, and recombines the results exactly. A dense
statevector of the whole circuit is used here only as an independent check.
"""

import numpy as np

from menos import Circuit, Opcode, RunOptions, run
from menos._core import Observable, PauliString

per_cluster = 4
n = 2 * per_cluster

circuit = Circuit(n, 0)
for q in range(n):
    circuit.append(Opcode.RY, [q], [0.21 + 0.07 * q])
for a in range(per_cluster):                     # cluster A: qubits 0..3
    for b in range(a + 1, per_cluster):
        circuit.append(Opcode.CZ, [a, b])
for a in range(per_cluster, n):                  # cluster B: qubits 4..7
    for b in range(a + 1, n):
        circuit.append(Opcode.CZ, [a, b])
circuit.append(Opcode.T, [per_cluster])
for b in range(per_cluster, n):                  # qubit 0 migrates into cluster B
    circuit.append(Opcode.CZ, [0, b])

# The observable Z on every qubit.
pauli = PauliString()
pauli.num_qubits = n
pauli.x_words = [0]
pauli.z_words = [(1 << n) - 1]
observable = Observable()
observable.terms = [(pauli, complex(1.0))]

options = RunOptions()
options.expectation = observable
cut = run(circuit, options)

report = cut.explain()
print(next(line for line in report.splitlines() if line.startswith("strategy")))
print(next(line for line in report.splitlines() if line.startswith("cut")))
print("expectation:", cut.expectation_value.real)

# Independent check: the dense statevector, contracted with the same observable.
dense_options = RunOptions()
dense_options.want_statevector = True
state = run(circuit, dense_options).statevector
parity = np.array([bin(i).count("1") & 1 for i in range(state.size)])
reference = float(np.sum(np.abs(state) ** 2 * (1 - 2 * parity)))

assert "cut_and_recombine" in report
assert abs(cut.expectation_value.real - reference) < 1e-12
print("agrees with the dense statevector to", abs(cut.expectation_value.real - reference))
