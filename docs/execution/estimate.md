# Estimate

Estimation asks every candidate engine the same question about the same job: *can you run this, and what would it cost?*

## What an estimate contains

| Field | Meaning |
|---|---|
| `feasible` | Whether the engine can serve this request at all |
| `peak_bytes` | Peak memory the plan would need |
| `work_units` | Cost in the model's shared unit |
| `infeasible_reason` | Why not, when `feasible` is false |

For a plan that may run on a GPU there is also a device-side record: device bytes, bytes transferred each way, and the number of kernel launches.

## Why an engine may be infeasible

The reason string is part of the report, so the loser is always explained:

| Reason | Example |
|---|---|
| Does not support the output | `the stabilizer engine does not track global phase -- only the dense engine can produce a statevector` |
| Does not support an operation | `operation 8 (t) is not a Clifford gate` |
| Too large to represent | `checked_pow2: n = 100 exceeds the maximum of 62` |
| Would not fit the memory budget | `direct dense simulation requires 17592186044416 bytes, budget is 7752519680 bytes` |
| Exceeds the time budget | `estimated runtime … s exceeds the job's time budget of … s` |
| Contradicted by a run | `observed runtime of at least … s exceeds the job's time budget` |

## Checked arithmetic

Cost estimates involve powers of two that overflow quickly. Every estimate uses checked arithmetic: a quantity that cannot be represented makes the candidate **infeasible with a reason**. It never wraps around, and it never throws an exception the caller has to catch.

## How each engine estimates

Each engine computes its estimate from structural facts (qubit count, operation counts, requested outputs) without executing anything.

- **Dense** prices each gate at `2^(n+k)` work units for a `k`-qubit gate on `n` qubits, plus initialization and the requested outputs, and needs `16·2ⁿ` bytes.
- **Stabilizer** is polynomial in qubit count: row initialization, one pass per gate, and Gaussian elimination over GF(2) when sampling.
- **Pauli propagation** bounds the worst-case number of terms an observable can split into before propagating anything, and refuses an unrepresentable bound.
- **MPS** bounds the entanglement (Schmidt rank) rigorously from the circuit's structure. In exact mode it is feasible only if that bound fits the budget.
- **Tensor network** searches for a contraction order and prices the largest intermediate tensor; the search is stopped at the memory budget.

The formulas are in [Cost Model](/planning/cost-model).

## Memory is checked before allocation

Estimation is where the [memory budget](/planning/memory-constraints) is applied. A candidate whose peak bytes exceed it is infeasible here, before an allocation is ever attempted.

## Estimates are not seconds

Work units rank candidates for one job against each other. Converting them to seconds requires [calibration](/planning/cost-model#calibration), and that conversion is only used for time budgets and CPU-versus-CUDA selection.
