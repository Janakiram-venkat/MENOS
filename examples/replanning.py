"""Runtime monitoring and replanning when a run overruns its time budget.

The planner estimates a run's cost from a model. When the model is too
optimistic, a run can overrun its budget; MENOS then stops it at a safe
boundary, feeds what it observed back into the planner, and restarts under a
different plan -- or refuses the job if no plan fits.

This job is one where the model underestimates matrix-product-state (MPS)
execution, so the outcome depends on how fast your machine is. Every outcome
below is a correct result; the script checks the invariants that hold in all of
them and prints which one happened.
"""

from menos import Circuit, Opcode, RunOptions, run
from menos.exceptions import ResourceExceededError

n, layers, shots = 16, 8, 64
circuit = Circuit(n, n)
for layer in range(layers):
    for q in range(n):
        circuit.append(Opcode.H, [q])
    circuit.append(Opcode.T, [0])
    for q in range(layer % 2, n - 1, 2):
        circuit.append(Opcode.CX, [q, q + 1])
for q in range(n):
    circuit.append_measure(q, q)

options = RunOptions()
options.shots = shots
options.seed = 5
options.time_budget_seconds = 0.2  # bounds ONE execution attempt, not the whole job

try:
    result = run(circuit, options)
except ResourceExceededError as error:
    print("outcome: refused -- the budget was overrun and no other plan fits")
    print(str(error)[:160], "...")
else:
    assert sum(result.counts.values()) == shots and result.exact
    report = result.explain()
    if "replan 0" in report:
        print("outcome: a run overran its budget and was restarted under a new plan")
        print("\n".join(line for line in report.splitlines() if "replan" in line or line.startswith(" ")))
    else:
        print("outcome: the first plan finished inside the budget; nothing was replanned")
    print(next(line for line in report.splitlines() if line.startswith("strategy")))
