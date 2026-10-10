"""A 60-qubit Clifford circuit, which only the stabilizer engine can run."""

import random

from menos import Circuit, Opcode, RunOptions, run

n, depth = 60, 12
rng = random.Random(5)
circuit = Circuit(n, n)
for _ in range(depth):
    for q in range(n):
        circuit.append(rng.choice([Opcode.H, Opcode.S, Opcode.X, Opcode.Z]), [q])
    order = list(range(n))
    rng.shuffle(order)
    for i in range(0, n - 1, 2):
        circuit.append(Opcode.CX, [order[i], order[i + 1]])
for q in range(n):
    circuit.append_measure(q, q)

options = RunOptions()
options.shots = 16
options.seed = 1
result = run(circuit, options)

report = result.explain()
strategy = next(line for line in report.splitlines() if line.startswith("strategy"))
print(strategy)
print("distinct outcomes in 16 shots:", len(result.counts))

assert "direct_stabilizer" in strategy
assert sum(result.counts.values()) == 16
assert result.exact
