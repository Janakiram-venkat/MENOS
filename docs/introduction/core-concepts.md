# Core Concepts

A short glossary of the ideas the rest of the documentation builds on.

## Circuit, KIR, operation

A circuit is an ordered list of *operations* over numbered qubits and classical bits. An operation is a named **gate**, a **unitary** given as an explicit matrix (up to five qubits), or a **measurement**. See [KIR](/concepts/kir).

## Job and output request

A *job* is a circuit plus what you want out of it: measurement **counts** (`shots > 0`), the exact **statevector**, or an **expectation value** of an observable. A job must request at least one output. The request shapes the plan: only the dense engine can produce a statevector, and only some engines can produce an expectation value.

## Engine and backend

An **engine** is an algorithm (dense, stabilizer, Pauli propagation, MPS, tensor network). A **backend** is the hardware it runs on (CPU or CUDA). They are chosen separately: the algorithm by work units, then the backend by calibrated seconds.

## Plan, strategy, region

A **plan** is the fully decided way to run a job. Its **strategy** is one of `direct_dense`, `direct_stabilizer`, `direct_pauli`, `direct_mps`, `direct_tensor_network`, `factorized`, or `cut_and_recombine`. A **region** is a set of qubits run together on one engine. A direct plan has one region; a factorized or cut plan has several.

## Candidate

Every strategy the planner considered, feasible or not. The plan report lists all of them. See [Plan Explanation](/planning/explain).

## Work unit

The cost model's shared currency. A work unit is comparable across engines, which is what lets the planner choose between them. It is *not* seconds; seconds come from calibration. See [Cost Model](/planning/cost-model).

## Feasible and infeasible

A candidate is **infeasible** if it cannot serve the request: it would exceed the memory budget, it does not support a gate or an output, or it would exceed the time budget. Infeasible candidates carry a reason string.

## Budget

A constraint on a run. A **memory budget** bounds the bytes a plan may need. A **time budget** bounds the wall-clock seconds of *one execution attempt*. Both are checked at plan time; the time budget is also checked during execution. See [Memory Constraints](/planning/memory-constraints) and [Time Budgets](/runtime/time-budgets).

## Exact

A run is exact if no approximation was made. Every engine reachable from Python is exact, and `Result.exact` reports it. See [Exactness](/concepts/exactness).

## Replan

When a run overruns its time budget it is stopped and planned again using what was observed. The abandoned attempt is reported. See [Replanning](/runtime/replanning).

## Deterministic

The same circuit, options and seed always produce the same counts and the same plan. See [Determinism](/concepts/determinism).
