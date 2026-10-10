# Tensor Network

The tensor-network engine computes an **expectation value** by contracting the closed network formed by the circuit, its conjugate and the observable down to a single number.

## Capabilities

| Output | Supported |
|---|---|
| Expectation value | **Yes** (only) |
| Counts | No |
| Statevector | No |

It is enumerated only for expectation requests. It is always exact: contraction performs no truncation.

## How it works

For each Pauli term, MENOS builds the network for `⟨ψ|P|ψ⟩` and finds an order in which to contract it using a **min-fill elimination heuristic**. The cost of that order is governed by how wide the largest intermediate tensor gets, a quantity related to the network's *treewidth*. The peak intermediate size is what the engine reports as its memory.

## Feasibility

- The search for a contraction order is **bounded by the job's memory budget**: it stops as soon as an intermediate tensor would exceed it. A stopped search is reported as such ("the search was stopped early, not that no valid contraction plan exists") rather than as a claim that the circuit is impossible.
- An unrepresentable tensor size is reported as infeasible through checked arithmetic.

## When it helps

Contraction is cheap when the circuit's structure keeps intermediate tensors small, for example shallow or locally connected circuits, and expensive when it does not. It competes on the same work-unit basis as every other engine, so it is chosen only when its estimated cost is the lowest. It is enumerated on every expectation request, and in many circuits another strategy is cheaper.

## Scope

- Expectation values only.
- One contraction per observable term.
- Safe monitoring boundary is **per observable term**, not per operation: a run with a single-term observable cannot be interrupted mid-contraction. See [Monitoring](/runtime/monitoring).

## Related

[MPS](/engines/mps) · [Cut and Recombine](/engines/cut-and-recombine) · [Expectation Values](/build/expectation-values)
