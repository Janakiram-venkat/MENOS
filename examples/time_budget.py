"""Time and memory budgets: what they accept, and what they refuse."""

import math
import random

from menos import Circuit, Opcode, RunOptions, run
from menos.exceptions import ResourceExceededError

n = 14
rng = random.Random(1)
circuit = Circuit(n, 0)
for _ in range(3):
    for q in range(n):
        circuit.append(Opcode.RY, [q], [rng.uniform(0, math.pi)])
    for q in range(0, n - 1, 2):
        circuit.append(Opcode.CX, [q, q + 1])


def options(**fields):
    request = RunOptions()
    request.want_statevector = True
    for name, value in fields.items():
        setattr(request, name, value)
    return request


# 1. A generous time budget changes nothing except the report.
result = run(circuit, options(time_budget_seconds=60.0))
report = result.explain()
print(next(line for line in report.splitlines() if line.startswith("time_budget")))
assert "time_budget" in report
assert len(result.statevector) == 2**n

# 2. A budget no candidate can meet is refused before any work is done.
try:
    run(circuit, options(time_budget_seconds=1e-12))
except ResourceExceededError as error:
    print("time budget refused:", str(error)[:110], "...")
else:
    raise AssertionError("an impossible time budget must be refused")

# 3. Memory is budgeted in the same way: 2**14 amplitudes need 256 KiB.
try:
    run(circuit, options(memory_budget_bytes=1024))
except ResourceExceededError as error:
    print("memory budget refused:", str(error)[:110], "...")
else:
    raise AssertionError("an impossible memory budget must be refused")
