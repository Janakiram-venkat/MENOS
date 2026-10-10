# Replanning

A run that overran its time budget is abandoned at a safe boundary and restarted under a different plan.

<<< @/../examples/replanning.py

## Output

This is real output from the development machine:

```text
outcome: a run overran its budget and was restarted under a new plan
replan 0   threshold=0.2 s crossed at elapsed=0.224734 s
           abandoned after 194 safe boundaries on engine=mps backend=cpu
           was  strategy=direct_mps predicted=0.000835369 s
           reason="observed runtime exceeded the time budget; mps on cpu is excluded by an observed floor of 0.224734 s"
           now  strategy=direct_dense
strategy   direct_dense (exact)
```

The model predicted the MPS engine would need under a millisecond. It was still running at 0.2247 s, so it was abandoned, the planner was asked again with that observation, dense was estimated to fit, and the job completed correctly. The whole job took about 0.44 s, more than the 0.2 s budget, because the budget bounds one attempt.

::: warning The outcome depends on your hardware
This job is one where the planner's model is far too optimistic, so the result depends on how fast your machine is. On a faster machine the MPS run may finish inside the budget and nothing is replanned; on a slower one the replacement may also overrun and the job be refused with `ResourceExceededError`. The script accepts every outcome and prints which happened. All of them are correct.
:::

## See also

[Replanning](/runtime/replanning) · [Monitoring](/runtime/monitoring) · [Time Budgets](/runtime/time-budgets)
