# ADR 0011: Contraction path — hand-rolled heuristic versus a library

## Status

Accepted. Phase 8 acceptance criterion 5 requires this comparison to be
explicit and evidence-based rather than a default asserted without argument.

## Context

The tensor-network engine needs a contraction ordering. Finding the best one
is NP-complete — contraction complexity equals the treewidth of the network's
line graph (Markov & Shi; see `docs/adr/0012` for the full statement and
citation), and computing treewidth exactly is NP-hard. So every option is a
heuristic; the question is only whose.

Two candidate directions:

1. The greedy minimum-fill-in ordering MENOS already has, written here.
2. A reference contraction-path library — `cotengra` or `opt_einsum` are the
   established choices — which implement materially more sophisticated
   searches: simulated annealing, hypergraph partitioning (KaHyPar),
   community detection, and reconfiguration of an initial greedy path.

The prime directive on dependencies requires an ADR stating why writing it
here is worse before taking one on, so the burden of proof sits on adding the
dependency, but the comparison still has to be made rather than assumed.

## The decisive architectural fact

`cotengra` and `opt_einsum` are **Python** packages. MENOS's contraction runs
in the C++ core, which has no Python at execution time: the Python layer is a
Qiskit adapter over the public C++ API (`docs/adr/0004`), not something the
engine can call into. Using either library would mean one of

- a Python round trip inside the engine's own `estimate()` and `execute()`,
  making the C++ core depend on a Python runtime, inverting the dependency
  direction the architecture rests on; or
- reimplementing the library's search in C++, which is not "adding a
  dependency" at all — it is the hand-rolled option with a different
  heuristic inside it.

There is no mainstream C++ contraction-path library with the standing
`cotengra` has in Python. So the realistic choice is not "ours versus
theirs"; it is "this heuristic, or a different hand-rolled one".

## Evidence on whether the heuristic we have is adequate

Measured, not assumed.

**Exactness against known treewidths.** `tests/analysis/treewidth_test.cpp`
checks min-fill against graphs whose treewidth is a documented result: a path
(1), a cycle (2), the complete graph K6 (5), and 3x3 and 4x4 grids (3 and 4).
It reproduces all five exactly. This validates the search's arithmetic — and
only that; see the limitation recorded in `docs/adr/0012` about what such a
suite does not establish.

**On the workload Phase 8 is accepted against.** For the balanced-binary-tree
family (`tests/api/tensor_network_scale_test.cpp`), at n=31 and depth 4, the
ordering min-fill finds needs a peak intermediate of 32 KB, against dense's
34.4 GB and MPS's roughly 80 GB. The margin is six orders of magnitude. A
better ordering would shrink an already negligible number; it would not
change a single planner decision.

**Where it is genuinely weak.** On RCS-style brickwork circuits the found
width is 50 (~18 PB, `docs/adr/0010`'s correction). That is not a heuristic
failure that a better search would rescue: those circuits are constructed to
have high contraction complexity, and no ordering makes them tractable. A
reference library would report a similarly hopeless number more slowly.

**Cost of the search itself.** `benchmarks/tensor_network_cost_correlation_benchmark.cpp`
records planning overhead of 0.65 ms to 10.6 ms for ordinary expectation jobs
up to 20 qubits and depth 8. A more sophisticated search — annealing or
partitioning — costs materially more than greedy min-fill, and it would be
paid on every expectation job, including the ones that go on to choose a
different engine entirely.

## Decision

Keep the hand-rolled min-fill heuristic. Do not add `cotengra`,
`opt_einsum`, or an equivalent.

The reasoning is not "ours is as good"; on hard instances it is certainly
not. It is that the dependency would invert the core's architecture for a
gain that is unmeasurable on the workload this engine is accepted against,
and unattainable on the workload where the gap is real.

## When to revisit

This is a decision about evidence available now, and the evidence that would
overturn it is specific:

- A workload MENOS actually needs where min-fill's ordering is materially
  worse than a reference library's on the *same* network — demonstrated by
  running both, not argued from the literature's general superiority claims.
- "Materially worse" means a difference that changes a planner decision: the
  contraction moving from infeasible to feasible under a real budget, not a
  constant-factor improvement on something already cheap.
- If that appears, the first thing to evaluate is a C++ implementation of the
  better ordering heuristic, since the Python-dependency objection above
  stands regardless of how good the ordering is.
