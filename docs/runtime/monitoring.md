# Monitoring

Monitoring is how MENOS notices that a run is taking longer than its time budget allows. It is **only active when a job sets `time_budget_seconds`**. A job without one installs no monitor, runs exactly as before, and produces a byte-identical report.

## Why it exists

The planner's prediction of *time* can be wrong, sometimes by orders of magnitude. In one measured case the MPS engine was predicted to take 0.0019 s and took 2.27 s. A budget checked only at plan time would admit that job and then let it overrun silently. Monitoring makes the budget bind while the job runs.

## What is watched

One quantity: **wall-clock elapsed time of the current execution attempt**, compared with the job's time budget.

The threshold is the budget itself. No factor, margin or tuning constant is involved, so there is nothing to misconfigure. When elapsed time exceeds the budget, the attempt is abandoned.

## Where: safe boundaries

The monitor is consulted only at points where the engine's state is complete and consistent, so stopping there is safe and frees everything the engine owns. The boundary depends on the engine and on the phase of its work:

| Engine / phase | A safe boundary is |
|---|---|
| Dense, stabilizer, MPS, Pauli, CUDA, gate sweep | between two operations |
| Tensor network | between two observable terms |
| MPS and stabilizer counts sampling | between two sampled shots |
| Multi-region counts combination | between two shots |

It is **never** inside one gate application, one contraction or one matrix multiplication.

### On the GPU

A device gate application synchronizes before it returns, so no kernel is in flight at a boundary, and abandoning frees the device allocation exactly as the host one is freed.

### Why sampling is monitored too

Sampling can be most of a run. For the MPS engine it was 69% of wall time in one measured case. Monitoring only the gate sweep would leave most of the work unwatched, so the per-shot loops are boundaries as well.

## What is not monitored

- **Dense sampling.** It is a single pass over the state, about the cost of one gate, so it cannot overrun by more than one gate's worth.
- **Inside a tensor-network contraction.** A single-term observable therefore has only one boundary, at completion.

## Cost

A monitored run pays one clock read (about 31 ns) and one virtual call per boundary. On the cheapest engine in the project (a 200-qubit stabilizer run at about 1.3 µs of work per boundary) that is an arithmetic ceiling of about 2.4%, too small to resolve above run-to-run noise in measurements. Engines with more work per boundary show no measurable difference. Jobs with no budget pay one predictable branch per boundary.

## What happens on a crossing

The attempt is abandoned by an exception that unwinds through the engine, then the runtime layer above decides what to do. See [Replanning](/runtime/replanning).

## Related

[Time Budgets](/runtime/time-budgets) · [Replanning](/runtime/replanning) · [Cost Model](/planning/cost-model)
