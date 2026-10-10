# ADR 0010: Phase 8 precondition benchmark — is a tensor-network engine needed?

## Status

Accepted (as a record of evidence; the decision whether to proceed to
Phase 8 is the owner's, per `docs/BUILD_PLAN.md`'s own explicit
wording for this task).

## Context

`docs/BUILD_PLAN.md`'s "Phase 8 precondition: benchmark for demand"
requires evidence, gathered before any tensor-network engineering
starts, on whether a circuit class exists that every current candidate
(`DirectDense`, `DirectStabilizer`, `DirectPauli`, `DirectMPS`,
`Factorized`) genuinely cannot handle — `ResourceExceeded`, not merely
slow — for `counts` and `statevector` output. This is a benchmark-and-
report task only; no Phase 8 or Phase 9 code was written or scoped as
part of it, and no planner, engine, or public API file was touched.

## Method: cost-estimation only, never execution

`benchmarks/rcs_feasibility_boundary_benchmark.cpp` calls
`menos::cost::estimate_direct()` / `estimate_factorized()` directly —
the same functions the planner and `explain()`'s own candidate table
are built on — for every sweep point. **Nothing is ever executed.**
This is deliberate and load-bearing: every "infeasible" result comes
from `CostEstimate::feasible == false`, set when the estimated peak
bytes exceeds the budget or a checked-arithmetic overflow occurs while
computing the estimate — the same mechanism `ResourceExceeded` is built
on everywhere else in this codebase. Since nothing runs, there is no
wall-clock cost anywhere in this program to confuse with infeasibility:
"genuine `ResourceExceeded` versus just slow" cannot arise here by
construction, which is the guarantee this ADR relies on when it reports
a candidate as having "failed."

The report below distinguishes two different reasons a candidate can
be absent from "genuinely infeasible at this scale," since conflating
them would misread the result:
- **Structural exclusion**, present at every sweep point regardless of
  scale, not evidence of a scaling limit: `DirectStabilizer` (the
  circuit is deliberately non-Clifford from its first single-qubit
  rotation — `RX`/`RY`/`RZ` gates are never Clifford in this table),
  both `DirectStabilizer` and `DirectMPS` for a `statevector` request
  (neither engine can ever produce one — an eligibility rule, not a
  resource one), `DirectPauli` (never enumerated at all for `counts`/
  `statevector` requests — it only ever serves an expectation request,
  which this benchmark never asks for), `Factorized` (never enumerated
  unless the interaction graph has 2+ components; checked
  programmatically at every point below — it stayed at 1 component
  throughout, as expected for a connected 2D grid).
- **Genuine resource exhaustion at scale**: `DirectDense` (memory-bound
  purely by qubit count, `16 * 2^n` bytes, independent of depth) and
  `DirectMPS`'s own memory (bond dimension grows with entanglement,
  itself driven by depth).

Only the second kind counts toward "the boundary."

## RCS circuit generator

`benchmarks/rcs_circuit.h`/`.cpp`, `make_rcs_circuit(n, depth, seed)`:
a random-circuit-sampling brickwork circuit on a 2D grid of `n` qubits,
the standard construction used for quantum-supremacy-style stress
tests, not a bespoke one.

- **Topology**: row-major flat qubit index `r*C + c` on an `R x C`
  grid, `R = floor(sqrt(n))`, `C = ceil(n / R)` — only the first `n` of
  the `R*C` flat positions exist (the standard way to handle a
  non-square qubit count, matching how real non-rectangular chips are
  laid out).
- **Entangling gate**: `CZ` (order-independent, avoiding any
  operand-order ambiguity in the brickwork description).
- **Coupling pattern**: the standard 4-pattern cycle (horizontal-even
  columns, horizontal-odd columns, vertical-even rows, vertical-odd
  rows), repeating in that fixed order every 4 layers; a pair whose
  partner index is `>= n` (the grid's incomplete last row) is skipped.
- **Single-qubit rotation layer** (one per depth unit, before that
  depth's coupling layer, on every qubit): uniformly random among
  `RX`/`RY`/`RZ`, angle uniform in `[0, 2*pi)`, drawn from
  `common::Rng` (this codebase's own deterministic generator), never
  `<random>` directly.
- **Seed**: fixed constant `kRcsSeed = 20260926`.
- No `Measure` operations are appended — `counts` costing is driven by
  the `want_counts` request flag, not by literal `Measure` operations
  in the circuit (confirmed by reading `cost/cost_model.cpp`'s dense
  and MPS estimate paths before writing this benchmark); nothing here
  executes, so the Phase 3 64-classical-bit ceiling is irrelevant.

## Reference hardware

```
environment: os=Linux arch=x86_64 compiler=GCC 15.2.0, Debug cpu=AMD Ryzen 7 170 with Radeon Graphics
budget_bytes=7752519680
```

(`budget_bytes` is `menos_benchmark::safe_allocation_budget_bytes()`:
`kDefaultMemoryBudgetFraction` — 50% — of this machine's detected total
memory, the same default the planner itself uses when no explicit
budget is given.)

## Swept results

### Phase 1: qubit-count sweep, depth fixed at 30

Depth 30 was chosen so entanglement has saturated across the grid's
diameter well before the qubit counts being swept, making dense's own
qubit-count-only memory bound the binding constraint. The full
per-point candidate breakdown (every candidate's feasibility, cost or
reason) is reproduced verbatim below.

```
n=20 depth=30 components=1
  counts:
    Factorized: not enumerated (1 component)
    DirectDense: feasible peak_bytes=16777216 work_units=2237661184
    DirectStabilizer: infeasible (operation 0 (rz) is not a Clifford gate)
    DirectMPS: feasible peak_bytes=44739200 work_units=1173602271252
  statevector:
    Factorized: not enumerated (1 component)
    DirectDense: feasible peak_bytes=16777216 work_units=2237661184
    DirectStabilizer: infeasible (the stabilizer engine does not track global phase -- only the dense engine can produce a statevector)
    DirectMPS: infeasible (the MPS engine cannot produce a statevector -- reconstructing the full state defeats the reason to use it)
n=25 depth=30 components=1
  counts:
    Factorized: not enumerated (1 component)
    DirectDense: feasible peak_bytes=536870912 work_units=90664075264
    DirectStabilizer: infeasible (operation 0 (rz) is not a Clifford gate)
    DirectMPS: feasible peak_bytes=1252698752 work_units=99299656171545
  statevector:
    Factorized: not enumerated (1 component)
    DirectDense: feasible peak_bytes=536870912 work_units=90664075264
    DirectStabilizer: infeasible (the stabilizer engine does not track global phase -- only the dense engine can produce a statevector)
    DirectMPS: infeasible (the MPS engine cannot produce a statevector -- reconstructing the full state defeats the reason to use it)
n=30 depth=30 components=1
  counts:
    Factorized: not enumerated (1 component)
    DirectDense: infeasible (direct dense simulation requires 17179869184 bytes, budget is 7752519680 bytes)
    DirectStabilizer: infeasible (operation 0 (rz) is not a Clifford gate)
    DirectMPS: infeasible (direct mps simulation requires 45812984448 bytes, budget is 7752519680 bytes)
  statevector:
    Factorized: not enumerated (1 component)
    DirectDense: infeasible (direct dense simulation requires 17179869184 bytes, budget is 7752519680 bytes)
    DirectStabilizer: infeasible (the stabilizer engine does not track global phase -- only the dense engine can produce a statevector)
    DirectMPS: infeasible (the MPS engine cannot produce a statevector -- reconstructing the full state defeats the reason to use it)
  >>> boundary reached: n=30, depth=30 -- every candidate infeasible for both counts and statevector
```

**Boundary: n=30** (swept in steps of 5, so the true crossover lies in
`(25, 30]`). A direct analytic check of dense's own formula,
`16 * 2^n` bytes against the 7,752,519,680-byte budget, confirms this
precisely: dense fits through `n=28` (4.29 GB) and first exceeds budget
at `n=29` (8.59 GB, vs. the 7.75 GB budget) — one qubit before the
swept step size caught it at `n=30`. This is dense's own
qubit-count-only bound; the sweep's job was to also confirm every other
candidate had already failed by the same point, which it had (`DirectMPS`'s
own memory bound is reached even earlier in the depth dimension — see
Phase 2).

### Phase 2: depth sweep, n fixed at 30

```
n=30 depth=5 components=1
  counts:
    Factorized: not enumerated (1 component)
    DirectDense: infeasible (direct dense simulation requires 17179869184 bytes, budget is 7752519680 bytes)
    DirectStabilizer: infeasible (operation 0 (rz) is not a Clifford gate)
    DirectMPS: feasible peak_bytes=14330496 work_units=5603743774
  statevector:
    Factorized: not enumerated (1 component)
    DirectDense: infeasible (direct dense simulation requires 17179869184 bytes, budget is 7752519680 bytes)
    DirectStabilizer: infeasible (the stabilizer engine does not track global phase -- only the dense engine can produce a statevector)
    DirectMPS: infeasible (the MPS engine cannot produce a statevector -- reconstructing the full state defeats the reason to use it)
n=30 depth=10 components=1
  counts:
    Factorized: not enumerated (1 component)
    DirectDense: infeasible (direct dense simulation requires 17179869184 bytes, budget is 7752519680 bytes)
    DirectStabilizer: infeasible (operation 0 (rz) is not a Clifford gate)
    DirectMPS: infeasible (direct mps simulation requires 45812984448 bytes, budget is 7752519680 bytes)
  statevector:
    Factorized: not enumerated (1 component)
    DirectDense: infeasible (direct dense simulation requires 17179869184 bytes, budget is 7752519680 bytes)
    DirectStabilizer: infeasible (the stabilizer engine does not track global phase -- only the dense engine can produce a statevector)
    DirectMPS: infeasible (the MPS engine cannot produce a statevector -- reconstructing the full state defeats the reason to use it)
  >>> boundary reached: n=30, depth=10 -- every candidate infeasible for both counts and statevector
```

**Boundary: depth=10** at `n=30` — dense's own bound (memory, not
depth-dependent) was already exceeded at every depth swept, including
depth=5; what depth=10 adds is that `DirectMPS`'s own bond-dimension-
driven memory also exceeds budget by that point, confirming its own
resource limit is reached quickly (well within a depth range anyone
would plausibly run) once qubit count is already past dense's own
boundary. At depth=5, `DirectMPS` alone remained feasible for `counts`
(14 MB, low enough bond that shallow entanglement hadn't yet saturated)
— the only point in the entire sweep where a non-dense candidate was
the sole surviving option.

## Interpretation

- **The boundary is real and is `ResourceExceeded`, not slowness**: by
  this benchmark's own construction (cost-estimation only, nothing
  executed), there is no other way for a candidate to appear
  infeasible here.
- **The boundary is small**: `n=30`, depth as low as 10, on a machine
  with a 7.75 GB budget (50% of this machine's detected memory). This
  is almost entirely explained by `DirectDense`'s own `16 * 2^n` bound
  — an RCS-style circuit this deeply entangled defeats `DirectMPS` and
  `DirectStabilizer`/`DirectPauli` (the latter two structurally, not by
  scale) well before dense's own memory bound is reached, so dense's
  ordinary exponential wall is what this benchmark actually measured.
  A machine with more available memory would push this boundary out
  by exactly dense's own `16 * 2^n` formula (roughly 1-2 more qubits
  per doubling of budget) — this boundary is a direct, mechanical
  restatement of "how much memory does this machine have," not
  evidence of a gap none of the *other* four candidates could have
  filled at a larger budget.
- **No candidate other than dense ever came close to defining this
  boundary.** `DirectMPS` remained feasible well past the point dense
  first became infeasible (feasible at n=25/depth=30, and at
  n=30/depth=5), meaning MPS is the *more* memory-efficient candidate
  here, not the bottleneck; it is dense's own exponential memory need
  that this sweep actually found the wall of.

## Recommendation

**This benchmark does not, by itself, establish a case for Phase 8.**
The boundary found is dense's own well-known exponential memory wall,
reached at a small qubit count on this machine's specific (50%-of-
detected-memory) budget — not a gap that a tensor-network engine would
close differently from simply running on a machine with more memory,
or from Phase 10's GPU backend (which speeds up every existing engine,
including a future tensor-network one, and per the spec is "very
likely the cheaper fix if the actual complaint is speed, not
feasibility" — though speed is explicitly not what this benchmark
measured). A genuine case for Phase 8 would need evidence that some
circuit class stays infeasible even on a machine with a much larger
memory budget, where `DirectMPS`'s own entanglement-driven bound (not
dense's raw qubit-count wall) becomes the binding constraint — the
depth=5 data point above (`DirectMPS` alone surviving on `counts`)
is the one signal in this sweep pointing in that direction, but it was
never isolated as *the* boundary here since dense's bound was reached
first on this machine.

This is evidence for you to weigh, not a decision: whether a
tensor-network engine is worth building depends on whether MENOS's
actual target workloads and target hardware (larger-memory machines,
where dense's wall recedes and MPS's own entanglement-driven wall
would need to be probed directly) make this gap real for you, which
this benchmark cannot determine on its own.

## Reproducing this result

```
cmake --preset dev -DMENOS_BUILD_BENCHMARKS=ON
cmake --build --preset dev
./build/dev/benchmarks/menos_rcs_feasibility_boundary_benchmark
```

Deterministic given the fixed seed (`20260926`) and this machine's
detected memory (which sets `budget_bytes`) — a different machine will
find a different `n` boundary via the same `16 * 2^n` relationship, not
a different generator output.

---

## Step 2 addendum: analytic contraction-cost estimate

Step 1 found infeasibility at n=30, depth=10 but could not tell whether
that is a genuine complexity wall (hard for every classical method,
tensor network included) or an artifact of the benchmark machine's
limited memory (only hard for MENOS's current five methods).
`docs/BUILD_PLAN.md`'s "Phase 8 precondition, step 2" answers this with
a treewidth-based analytic estimate of the peak intermediate tensor a
real tensor-network contraction would need, for the exact boundary
circuit plus a shallower-depth sweep at the same qubit count. This is
analysis only: no tensor-network code was written, no planner or
engine file was touched, and no new dependency was added.

### Method

`benchmarks/treewidth_estimate.h`/`.cpp` (new files, outside
`include/menos/`, outside the planner, every engine, and the public
API — linking only `menos_ir` and `menos_common`, never becoming a
sixth candidate), used by
`benchmarks/treewidth_contraction_estimate_tool.cpp` (a standalone
`main()`, reusing step 1's own `benchmarks/rcs_circuit.h` generator
directly rather than a second, duplicated one).

**Tensor-network graph** — distinct from Phase 3's
`analysis::build_interaction_graph` (one node per *qubit*, a different
question entirely: whether the circuit splits into independent
regions). Here: one node per gate operation (`GateOp`/`UnitaryOp`;
`MeasureOp` excluded as a classical readout, moot for this circuit
since the RCS generator never appends one), one undirected edge per
wire between temporally consecutive operations on the same qubit —
exactly the "gate graph" / line-graph structure used in the
treewidth-based quantum-circuit-simulation literature (Markov & Shi,
"Simulating quantum computation by contracting tensor networks,"
2008).

**Elimination heuristic** — greedy minimum-fill-in: at each step,
eliminate the remaining vertex whose removal adds the fewest new
fill-in edges among its neighbors (ties broken by lowest vertex index,
for determinism), connect its remaining neighbors to each other, and
remove it. This is one of the two classic, well-established treewidth
heuristics in the graph-theory literature (alongside minimum-degree;
see Bodlaender & Koster's treewidth-heuristics survey), and the more
commonly used default in the quantum-circuit-treewidth literature this
step's own spec invokes — **not improvised for this task**. The
reported width (the maximum degree any vertex had at the moment of its
own elimination) is a heuristic **upper bound** on the graph's true
treewidth, never an exact value or a lower bound: a large result would
be suggestive, not conclusive, since a better ordering might exist
that this greedy heuristic did not find. (A small result, which is
what was actually found below, does not have this caveat in the same
direction — a valid elimination ordering of width `w` is a real,
checkable certificate that a contraction of that peak size exists,
regardless of whether a smaller one also does.)

**Peak tensor size** — `16 * 2^width` bytes (complex128): the standard
treewidth-to-contraction correspondence (an elimination ordering of
width `w` corresponds to a contraction sequence whose largest
intermediate tensor has at most `w` dangling indices, each of
dimension 2), the same unit and formula shape as dense's own
`16 * 2^n`, so the two numbers are directly comparable as the spec
asks.

### Self-tests (run automatically before trusting the tool on the real circuit)

Hand-verifiable graphs with known-exact treewidth from graph theory
(not asserted from memory): a path (treewidth 1, any tree), a cycle
(treewidth 2), the complete graph `K_6` (treewidth 5, trivially), and
3x3/4x4 grid graphs (treewidth 3/4 respectively — a standard,
documented result, directly relevant since the real circuit is
grid-based).

```
=== Self-tests (known-exact treewidth) ===
  [PASS] path graph, n=10 (treewidth 1): expected width 1, got 1
  [PASS] cycle graph, n=10 (treewidth 2): expected width 2, got 2
  [PASS] complete graph K_6 (treewidth 5): expected width 5, got 5
  [PASS] 3x3 grid graph (treewidth 3): expected width 3, got 3
  [PASS] 4x4 grid graph (treewidth 4): expected width 4, got 4
all self-tests passed
```

All five passed exactly (heuristic width equals the known-exact value
in every case — expected for graphs this simple, where min-fill has no
room to do worse than optimal).

### Swept results: depth sweep at n=30 (step 1's boundary was depth=10)

```
n=30 depth=2 graph_nodes=85 graph_edges=80 min_fill_width=1 peak_tensor_size=32 bytes (exact)
n=30 depth=4 graph_nodes=169 graph_edges=188 min_fill_width=7 peak_tensor_size=2048 bytes (exact)
n=30 depth=6 graph_nodes=254 graph_edges=298 min_fill_width=10 peak_tensor_size=16384 bytes (exact)
n=30 depth=8 graph_nodes=338 graph_edges=406 min_fill_width=16 peak_tensor_size=1048576 bytes (exact)
n=30 depth=10 graph_nodes=423 graph_edges=516 min_fill_width=21 peak_tensor_size=33554432 bytes (exact)
```

| depth | min-fill width (heuristic upper bound) | peak tensor size | vs. dense's own bound at n=30 (17,179,869,184 bytes) |
|---|---|---|---|
| 2 | 1 | 32 bytes | dense is 536,870,912x larger |
| 4 | 7 | 2,048 bytes | dense is 8,388,608x larger |
| 6 | 10 | 16,384 bytes | dense is 1,048,576x larger |
| 8 | 16 | 1,048,576 bytes (1 MB) | dense is 16,384x larger |
| 10 | 21 | 33,554,432 bytes (32 MB) | dense is 512x larger |

Reference hardware: same machine as step 1 (`environment: os=Linux
arch=x86_64 compiler=GCC 15.2.0, Debug cpu=AMD Ryzen 7 170 with Radeon
Graphics`) — irrelevant to the result itself, since this step never
allocates or executes anything, but recorded per the same benchmark
rule step 1 followed.

### What the numbers suggest

At the exact circuit and depth that made step 1's benchmark find every
current MENOS candidate infeasible (n=30, depth=10), the min-fill
heuristic finds a contraction ordering whose peak intermediate tensor
is **32 MB** — 512 times *smaller* than dense's own 17.18 GB
requirement at that qubit count, and the width grows only modestly
(roughly linearly, 1 -> 7 -> 10 -> 16 -> 21) across the whole depth
sweep, showing no sign of the sharp exponential blow-up that would
indicate a genuine complexity wall in this depth range. This is a
heuristic upper bound, not a guarantee that 32 MB is the true minimum,
but it is a real, checkable one: an elimination ordering of width 21
was actually found, meaning a contraction achieving that peak size
provably exists for this exact circuit, regardless of whether a still-
better ordering also does.

This is the opposite of an inconclusive result: it directly answers
step 1's open question for this specific circuit class at this depth.
The n=30, depth=10 boundary step 1 found is **dense's own ordinary
exponential-memory limitation**, not evidence that this circuit class
is fundamentally hard to simulate classically — a tensor-network
engine would handle this exact circuit with several orders of
magnitude less memory than dense needs. Whether that gap is worth
closing with a real Phase 8 engine — and whether RCS-style circuits at
this depth are actually representative of MENOS's target workloads —
is not something this analysis decides; that stays the owner's call,
per the spec's own explicit wording, and is not recommended here in
either direction.

### Reproducing this result

```
cmake --preset dev -DMENOS_BUILD_BENCHMARKS=ON
cmake --build --preset dev
./build/dev/benchmarks/menos_treewidth_contraction_estimate_tool
```

Deterministic given step 1's own fixed seed (`20260926`, reused
exactly) — this tool never allocates or executes a circuit, so it does
not depend on the running machine's own memory the way step 1's sweep
did.

---

## Correction (found during Phase 8 implementation, not step 2 itself)

**Step 2's own numbers above are wrong.** They applied the min-fill
heuristic directly to the "one node per gate, one edge per wire" graph
(nodes = tensors). Verified against two independent published sources
during Phase 8 implementation (Markov & Shi, "Simulating Quantum
Computation by Contracting Tensor Networks," SIAM J. Computing 2008;
and the classical bucket-elimination literature, e.g. Dechter 1999):
contraction complexity is bounded by the treewidth of that graph's own
**line graph** (nodes = wires/indices, edges = "these two wires meet at
a common tensor"), not the tensor-node graph directly. Applying min-fill
to the wrong graph understated the true width substantially.

Recomputed on the exact n=30, depth=10 boundary circuit from step 1,
against the correct line graph (`analysis::build_line_graph`, the
production implementation): **width 50**, not 21 — a peak tensor of
`16 * 2^50` bytes (~18 petabytes), not 32 MB. This is far larger than
dense's own 17.18 GB requirement at the same qubit count, not 512 times
smaller. The conclusion this step drew — "a tensor-network engine would
handle this exact circuit with several orders of magnitude less memory
than dense needs" — is false for this specific circuit. The n=30,
depth=10 RCS-style boundary circuit does not, after all, distinguish a
genuine tensor-network-shaped gap from dense's own ordinary memory
wall; it is just as infeasible for tensor-network contraction as for
every other candidate.

This does not mean a correctly-implemented tensor-network engine has no
value: circuits whose closed network's own line graph genuinely has low
treewidth are demonstrably tractable this way (verified directly,
Phase 8's own differential test suite, matching the dense oracle exactly
across every circuit and qubit count tested). It means this specific
circuit family, at this specific depth, is not such a case, and this
step's own claim that it was is retracted. Whether any of MENOS's actual
target workloads have low-enough line-graph treewidth to justify this
engine remains open (see docs/BUILD_PLAN.md Phase 8's own updated
section and its acceptance criteria).

`benchmarks/treewidth_estimate.h`/`.cpp` and
`benchmarks/treewidth_contraction_estimate_tool.cpp` compute the same
(wrong) tensor-node-graph quantity this correction describes; they are
superseded by `analysis::treewidth.h`'s own production `LineGraph`/
`build_line_graph` and are not being kept as a "how to reproduce this"
pointer past this correction.
