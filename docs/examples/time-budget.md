# Time Budget

What time and memory budgets accept and what they refuse.

<<< @/../examples/time_budget.py

## Output

```text
time_budget60 s
time budget refused: plan_job: no feasible candidate within the time budget of 0.000000 s -- estimated runtime 0.011243 s exceeds t ...
memory budget refused: plan_job: no feasible candidate -- direct dense simulation requires 262144 bytes, budget is 1024 bytes; the st ...
```

1. A generous budget (60 s) is accepted; the report gains a `time_budget` line.
2. A budget no candidate can meet (`1e-12` s) is refused **before any work**: the message shows the estimate that blocked it.
3. A memory budget too small for a 14-qubit state is refused the same way, stating the bytes required and available.

The estimated seconds in the second line vary with your machine.

## See also

[Time Budgets](/runtime/time-budgets) · [Memory Constraints](/planning/memory-constraints)
