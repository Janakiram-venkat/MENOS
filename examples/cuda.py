"""Which backend did the planner pick? Works on CPU-only and CUDA builds alike."""

import math
import random

from menos import Circuit, Opcode, RunOptions, run

n = 20
rng = random.Random(3)
circuit = Circuit(n, 0)
for _ in range(3):
    for q in range(n):
        circuit.append(Opcode.RY, [q], [rng.uniform(0, math.pi)])
    for q in range(0, n - 1, 2):
        circuit.append(Opcode.CX, [q, q + 1])
    for q in range(1, n - 1, 2):
        circuit.append(Opcode.CX, [q, q + 1])

options = RunOptions()
options.want_statevector = True
result = run(circuit, options)

report = result.explain()
region = next(line for line in report.splitlines() if line.startswith("region 0"))
print(region)
print(f"execute: {result.execute_seconds:.3f} s")

# The Python API never names a backend; the plan records which one ran.
assert "engine=dense" in region
assert "backend=cpu" in region or "backend=cuda" in region
assert abs(sum(abs(a) ** 2 for a in result.statevector) - 1.0) < 1e-12
