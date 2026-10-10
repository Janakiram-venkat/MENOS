# ADR 0012: The tensor-network contraction model, and what it does not cover

## Status

Accepted, closing Phase 8.

## Context

Phase 8 was built once, shelved when its motivating evidence collapsed
(`docs/adr/0010`'s correction), and then completed. This ADR records the
model as it finally stands, the correction that was needed to make its cost
estimate sound, and the limitations a reader should know before trusting it.

`CLAUDE.md`'s testing rules now require that any ADR introducing a metric
state which real quantity it bounds and why its validation exercises that
quantity. This document is partly the reason that rule exists.

## What is contracted

For an expectation request, per observable term, the engine builds the
**closed** network for `<0| U^dagger P U |0>`: a `|0>` boundary tensor per
qubit, the circuit's gate tensors, one Pauli tensor per qubit the term acts
on non-trivially, the mirrored `U^dagger` gates, and a `<0|` boundary. Every
index is summed, so the result is a scalar per term; the runtime applies each
term's coefficient (`docs/adr/0007`).

A qubit the term is identity on gets no Pauli tensor — inserting an identity
is a mathematical no-op, so omitting it is a simplification, not an
approximation.

## The three quantities, kept distinct

This is the heart of the model, and conflating any two of them is what went
wrong the first time.

**1. Which graph governs cost.** Not the tensor-node graph (nodes = gates,
edges = wires). Contraction cost is governed by that graph's **line graph**:
nodes = wires/indices, edges = "these two wires meet at a common tensor", so
a k-legged tensor contributes a k-clique. The source, quoted:

> "A contraction sequence is an ordering of a tensor network's edges, and the
> complexity of a contraction sequence S is the largest degree of a merged
> vertex created by contracting the network according to S. The contraction
> complexity (cc) of a tensor network is the minimum complexity over all
> possible contraction sequences."
> "The contraction complexity of a graph equals the treewidth of its line
> graph."

— Markov & Shi's result as restated in "Benchmarking treewidth as a practical
component of tensor network simulations", PLOS ONE 13(12), 2018
(arXiv:1807.04599).

Precondition step 2 ran min-fill on the tensor-node graph and reported width
21 where the line graph gives 50. That single substitution is what made a
circuit look tractable when it was not.

**2. Memory feasibility — the peak intermediate tensor.** Under the ordering
we execute, eliminating a wire whose line-graph degree is `d` materializes an
intermediate over `d+1` indices: its `d` neighbours plus the wire being
summed away, which has to exist before it can be summed. So the peak is
`16 * 2^(w+1)` bytes for an order of induced width `w`, complex128.

The `+1` is a correction this phase made. The original estimate used
`16 * 2^w` and therefore understated peak memory by exactly a factor of two —
enough for the engine to claim it fits a budget it does not fit.

**3. Arithmetic cost — the work unit.** A different number from the peak.
Each elimination step costs at most `(d+1) * 2^(d+1)` scalar multiply-adds,
summed over steps, converted to the shared cost unit (`docs/adr/0006`) so the
planner can compare this candidate against dense, stabilizer, Pauli and MPS.

## Treewidth is not the quantity we estimate

A distinction worth stating plainly, because the vocabulary invites the
error. Treewidth is the *minimum* induced width over all elimination
orderings. MENOS does not achieve it, does not compute it, and must not quote
it as a cost: greedy min-fill returns some ordering, and what bounds MENOS's
actual run is the induced width **of that ordering**. Estimating treewidth
would understate what the engine will really do.

The estimator therefore reports the width of the order `execute()` will use —
the same order, from the same deterministic search, so estimate and execution
cannot drift apart.

## The algorithm

- **Ordering:** greedy minimum-fill-in on the line graph of the closed
  network. Ties break to the lowest node index — a fixed rule, never
  iteration or hash order. This is a correctness requirement, not tidiness:
  floating-point addition is not associative, so an unpinned order changes
  the result's last bits between identical runs.
- **Search bound:** the search stops as soon as the running width already
  implies a peak above the job's budget (`EngineEstimateRequest::budget_bytes`).
  Reported as "did not fit this budget", never as "no valid plan exists" —
  a stopped search proves nothing about orderings it never examined.
- **Execution:** bucket elimination in exactly that order. For each wire,
  resolve its two original endpoints to whichever live tensor currently holds
  each (union-find with path compression), then contract that pair, or
  self-trace when both ends already sit in the same tensor.
- **Complexity:** `V` elimination steps over `V` line-graph nodes; each scans
  remaining vertices for minimum fill and recomputes fill over the affected
  neighbourhood. `O(V * (V + sum d^2))`, i.e. `O(V^3)` worst case, with
  incremental fill caching keeping it well below that in practice.

## Why the planner does not special-case this engine

An earlier revision gated `DirectTensorNetwork` behind "only once every other
candidate is infeasible", because its `estimate()` runs a real combinatorial
search where every other engine's is closed-form, and enumerating it
unconditionally was measured to slow unrelated jobs.

That gate was removed. It contradicts the property that makes MENOS a
planner rather than a collection of engines: a candidate is selected because
its estimate wins, never because it exists or because nothing else is left.
The cost problem it was papering over is solved by the mechanism that
belongs to it — bounding the search by the job's own budget — and the
residual overhead is reported rather than hidden: 0.65 ms to 10.6 ms for
ordinary expectation jobs up to 20 qubits and depth 8
(`benchmarks/tensor_network_cost_correlation_benchmark.cpp`).

## Limitations

State these before trusting the engine.

- **Expectation values only.** No counts (needs sequential conditional
  re-contraction with fixed outcomes) and no statevector (materializing 2^n
  defeats the reason to use a contraction at all — the same reasoning that
  kept statevector off MPS).
- **The peak estimate is a sound but loosening bound.** Measured on the tree
  family: 512 versus 256 actual bytes at depth 2, but 1,048,576 versus 65,536
  at depth 6. It never under-estimates, which is what feasibility needs, but
  a job rejected as infeasible near the budget edge might in fact have fit.
- **No slicing.** When the peak exceeds budget the candidate is reported
  infeasible, exactly like any other candidate that does not fit. Slicing
  (cutting indices to trade time for memory) is the standard remedy and is
  not implemented.
- **RCS-style circuits are out of reach**, and no contraction-ordering work
  will change that. They are constructed to have high contraction complexity.
- **Single-threaded, CPU only.** If threading is added, the accumulation
  order must stay fixed or determinism breaks.
- **The ordering heuristic is greedy.** Adequate on the accepted workload by
  six orders of magnitude (`docs/adr/0011`), not optimal in general.

## The lesson that generalizes

The wrong-graph bug survived a validation suite that checked min-fill against
path, cycle, K6 and grid graphs with known treewidths — and passed all five.
The suite proved the search computed treewidth correctly. It could not
notice that treewidth *of the wrong graph* was being computed, because it
never compared the metric against the thing the metric was supposed to
predict.

The peak-byte factor-of-two survived for the same reason: `explain()` printed
the estimated and actual peaks side by side and nothing asserted a
relationship between them. The test that finally caught it
(`TensorNetworkEngineTest.EstimatedPeakBytesBoundTheActualPeakAcrossRandomCircuits`)
compares the estimate against `execute()`'s own independently measured peak,
and failed on the old model before passing on the corrected one.

Hence `CLAUDE.md`'s rule: validating a metric's arithmetic is not validating
the metric. A new metric must say which real quantity it bounds, and its
tests must exercise that quantity.
