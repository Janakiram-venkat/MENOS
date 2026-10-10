# Cost Model

The cost model is what makes candidates comparable. It answers two different questions with two different currencies.

| Question | Currency | Used for |
|---|---|---|
| *Which algorithm is cheapest for this job?* | **work units** | Choosing the strategy |
| *How long will it take, in seconds?* | **seconds**, from calibration | Time budgets, CPU vs CUDA |

## Work units

A work unit is an analytic count of basic operations, computed from structural facts about the job only: qubit count, the number of operations and how many qubits each touches, and which outputs are requested. It involves no measurement and is identical on every machine.

All engines count in the same spirit so that their numbers can be ranked against each other for *the same job*. The units are **not** claimed to be wall-clock equivalent: a stabilizer word operation and a dense complex multiply-add are different things. What the unit promises is a consistent ranking.

```text
Dense       2ⁿ  +  Σ_gates 2^(n+k)  +  output terms             (exponential in n)
Stabilizer  n  +  gates·n  +  2·n²·⌈n/64⌉  (+ sampling)       (polynomial in n)
```

Pauli propagation, MPS, tensor network and cut estimates follow the same principle with their own structural inputs: the worst-case term count, the rigorous bond-dimension bound, the contraction order's largest tensor, and the sum over fragment evaluations. All use **checked arithmetic**, so an unrepresentable quantity makes a candidate infeasible with a reason and never wraps.

## Why the choice does not depend on timing

The planner picks the algorithm by work units alone. Timing never enters that decision, which keeps algorithm choice deterministic and identical across machines.

## Calibration

Seconds come from a one-time calibration, performed the first time a job is planned in a process and then cached:

| Measured | How | Used for |
|---|---|---|
| Streaming memory bandwidth | Timed large copy | A floor on any candidate's runtime |
| Work units per second (CPU) | A real circuit through the real dense engine, dividing its own work units by elapsed time | Converting work units to seconds |
| Host↔device transfer rates | Pageable host memory, the path the backend really takes | CUDA estimates |
| Kernel-launch overhead | A tiny kernel, including constant upload and synchronize | CUDA estimates |
| Device work rate | A real circuit through the CUDA engine, launch overhead subtracted | CUDA estimates |

Calibration measures this machine's speed on the code that will actually run, instead of assuming a constant. It costs on the order of 0.1 s the first time in a process (measured at roughly 86 to 99 ms on the development machine) and microseconds thereafter.

### Seconds for a CPU candidate

```text
seconds = max( work_units / work_units_per_second ,  peak_bytes / memory_bandwidth )
```

The first term comes from calibration on a cache-resident circuit, so it is optimistic for very large states. The second is a physical floor: a candidate that materializes `peak_bytes` cannot finish sooner than that many bytes take to stream.

### Seconds for a CUDA candidate

```text
seconds = kernel_launches · launch_overhead
        + host_to_device_bytes / h2d_rate
        + work_units / device_work_rate
        + device_to_host_bytes / d2h_rate
```

The launch term is what makes small circuits slower on the GPU: each launch costs a fixed amount however little it does. A model without it would claim the GPU always wins.

## How accurate is it?

Measured on the development machine, the ratio of actual to predicted seconds:

::: warning Disclaimer
These figures come from benchmarks run on the developer's own machine. Treat them as indicative; on your hardware they will differ.
:::


| Engine | Actual ÷ predicted |
|---|---|
| Dense | 0.77 to 0.88 |
| Stabilizer | 1.4 to 1.6 |
| Factorized | 0.70 to 1.33 |
| **MPS** | **42 to 1,167** |

The dense, stabilizer and factorized time models are sound to within a factor of about two. **The MPS time model is badly optimistic**, because its cost per operation rises as the bond dimension grows, and the model does not capture that. This is a property of the model, repeatable to under 1%, not noise.

Calibration itself varies about 1.5× between processes.

These facts are why a time budget is enforced while the run executes, not just at plan time. See [Time Budgets](/runtime/time-budgets) and [Monitoring](/runtime/monitoring).

## Memory is exact

By contrast, predicted peak bytes equalled actual peak bytes in every workload measured, because each engine allocates exactly what it estimates.

## Related

[Execution Plans](/planning/execution-plans) · [Backend Selection](/planning/backend-selection) · [Estimate](/execution/estimate)
