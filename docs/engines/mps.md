# MPS and Tensor Networks

Two engines avoid the dense state by working with a *network of small tensors*. This page covers the **matrix product state (MPS)** engine; the other is on its own page, [Tensor Network](/engines/tensor-network).

## What an MPS is

An MPS stores an `n`-qubit state as a chain of `n` small tensors. The memory it needs depends on the *entanglement* across the chain, measured by the **bond dimension**, not on `2ⁿ`. A weakly entangled state of 100 qubits fits in kilobytes. A highly entangled one needs a bond dimension that grows exponentially and is no smaller than a dense state.

## Capabilities

| Output | Supported |
|---|---|
| Counts | Yes (one shot at a time, by walking the chain) |
| Expectation value | Yes |
| Statevector | **No**: reconstructing the full state defeats the point of using an MPS |

Every gate MENOS supports is accepted, including `ccx`, `cp` and unitaries on up to five qubits.

## Exactness and feasibility

In the default **exact** mode the engine never truncates. Before executing, it computes a **rigorous bound** on how large the bond dimension can possibly get, from the circuit's structure alone, and the engine is feasible only if the resulting memory fits the budget. If the bound is too large the candidate is refused; it is not run and truncated.

A truncating mode, `Controlled(tolerance)`, exists in the C++ core: it discards small singular values and reports an a-posteriori error bound built from what it actually discarded. **The Python API cannot request it**, so every run from Python is exact and `Result.exact` is `True`.

## Real example

A 100-qubit circuit cannot have a dense statevector, and MENOS plans it onto MPS:

<<< @/../examples/mps.py

```text
strategy   direct_mps (exact)
state bytes actually used: 49792
candidate 0strategy=direct_dense feasible=false winner=false reason="checked_pow2: n = 100 exceeds the maximum of 62"
```

## MPS is not universally faster

Workload structure decides. The planner's *work-unit* estimate of MPS can be much too optimistic about *time*: in one measured case (16 qubits, 8 layers) the model predicted about a millisecond and the run took about 0.7 s, and dense was about 7 times faster than MPS at that size. At 24 qubits the picture reverses: MPS took about 2.3 s against about 48 s for dense.

That mismatch is exactly why time budgets are [monitored during execution](/runtime/monitoring) rather than trusted at plan time.

## Cost

Memory comes from the rigorous bond-dimension bound. Work is dominated by the singular value decompositions performed for every two-qubit interaction, which use LAPACK (`zgesdd`).

## Related

[Tensor Network](/engines/tensor-network) · [Monitoring](/runtime/monitoring) · [Exactness](/concepts/exactness) · [MPS example](/examples/mps)
