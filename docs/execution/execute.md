# Execute

The runtime executes a plan. It resolves each region to an engine and backend, runs it, and recombines the regional results.

## Direct plans

A `direct_*` plan has one region covering every qubit. The runtime gives that engine the region's operations, the output request and the seed, and returns its result. Before running it computes which measured qubit feeds which classical bit, so that a classical bit written more than once takes its **last** write.

## Factorized plans

A `factorized` plan has one region per group of non-interacting qubits, each already assigned its own cheapest feasible engine. Regions run **one at a time**, and each result is folded into the answer immediately:

| Output | Recombination |
|---|---|
| Statevector | Exact tensor product of the regional states, built into one global buffer with one region live at a time |
| Counts | Independent per-shot draws from each region's outcome distribution, combined bit-wise. Exact: the regions are independent |
| Expectation | For each observable term, the **product** of the regional values, then the coefficient-weighted sum (the mixed-product property) |

A region with no operations and no measurement is an *idle* region: a qubit that stays `|0⟩`. It needs no allocation and no engine.

## Global phase

A circuit's `global_phase` multiplies the whole statevector. For a factorized plan it is applied **once**, as a scalar over the recombined buffer, never per region. It cancels in counts and in expectation values, so it affects only the statevector.

## Cut plans

A `cut_and_recombine` plan runs each fragment once for every distinct assignment of its cut wires, on the cheapest engine for that fragment, and reconstructs the expectation value exactly from a signed, weighted sum. See [Cut and Recombine](/engines/cut-and-recombine).

## Invariants are checked

Before executing a factorized plan the runtime verifies that its regions are consistent with the circuit, and fails loudly if not. There is no silent fallback to a different plan.

## Timing and memory

The result records `execute_seconds` and `sample_seconds` of the attempt that succeeded, and `actual_state_bytes`, which is a genuine measurement (for a factorized run, the peak over regions plus the global buffer) rather than a copy of the estimate.

## With a time budget

If the job carries a time budget, the same engines report *safe boundaries* to a monitor while running. See [Monitoring](/runtime/monitoring).
