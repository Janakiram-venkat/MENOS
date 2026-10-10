"""A 100-qubit circuit: too large for a statevector, cheap as an MPS."""

import math
import random

from menos import Circuit, Opcode, RunOptions, run

n, layers = 100, 4
rng = random.Random(2)
circuit = Circuit(n, n)
for layer in range(layers):
    for q in range(n):
        circuit.append(Opcode.RY, [q], [rng.uniform(0, math.pi)])
    for q in range(layer % 2, n - 1, 2):
        circuit.append(Opcode.CZ, [q, q + 1])
for q in range(n):
    circuit.append_measure(q, q)

options = RunOptions()
options.shots = 32
options.seed = 1
result = run(circuit, options)

report = result.explain()
strategy = next(line for line in report.splitlines() if line.startswith("strategy"))
print(strategy)
print("state bytes actually used:", result.actual_state_bytes)
print(next(line for line in report.splitlines() if "strategy=direct_dense" in line))

# A dense statevector of 100 qubits cannot exist; the planner says why and uses MPS.
assert "direct_mps" in strategy
assert "strategy=direct_dense feasible=false" in report
assert result.exact
assert sum(result.counts.values()) == 32
