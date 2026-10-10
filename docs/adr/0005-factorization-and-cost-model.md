# ADR 0005: Factorization, work-unit cost model, and BitString

## Status

Accepted.

## Context

`docs/BUILD_PLAN.md` Phase 3 introduces the planner's first genuine
decision (`DirectDense` vs. `Factorized`), based on structural independence
in the circuit's interaction graph, plus the arbitrary-width `BitString`
count key replacing Phase 1's `uint64_t`. `docs/BUILD_PLAN.md` §3.10 names
this file `docs/adr/0003-factorization-and-cost-model.md`; `0003` is
already `default-memory-budget.md` (Phase 1), so this is `0005` — the next
free number (0001–0004 exist).

Every mathematical claim below was derived and independently verified
during this session — including one case where a first-draft algorithm
was proven wrong by hand before any code was written for it, and one case
where a real bug was found by the existing randomized differential test
suite after implementation, not assumed correct.

## Interaction graph and connected components (`src/analysis`)

**Definition.** One vertex per qubit. For every operation with ≥2 quantum
operands, an edge between every pair of operands. One-qubit operations and
measurements add no edge. A `Unitary` on `k` qubits connects all `k`.

**Determinism.** Built via union-find (union-by-size, path compression) —
but component numbering and each component's own qubit ordering come
entirely from the *outer scan order* (qubits `0..n-1`, ascending), never
from union-find's internal parent pointers (unspecified after path
compression). Scanning ascending means a component's smallest member is
always visited first, so the first time a given root is seen assigns the
next sequential component index, and pushing each subsequent member as
it's discovered keeps qubits within a component ascending too — both
properties for free, from the scan alone.

## Proof: independent components produce a tensor-product state

Region construction guarantees every multi-qubit operation's operands lie
in one component. So the circuit's full operator sequence factors as
`U = U_r ⊗ I ⊗ ... ` (block-diagonal per region) applied in program order.
Operators on disjoint tensor factors commute, and for a product state the
mixed-product property `(A⊗I)(I⊗B)|ψ_0⟩⊗|ψ_1⟩ = (Aψ_0)⊗(Bψ_1)` holds
regardless of application order. Since `|0...0⟩` is already the product
state `|0⟩^{k_0}⊗|0⟩^{k_1}⊗...`, applying each region's own (internally
correctly-ordered) operations independently and tensoring the results
reproduces `U|0...0⟩` exactly — exact linear algebra, holding regardless
of intra-region entanglement, requiring only the absence of cross-region
gates.

## Region construction and remapping (`src/plan/region_builder.cpp`)

Each region owns `local_to_global` (ascending, ⊆ component qubits),
`global_to_local` (sentinel `kNoLocalQubit` elsewhere), and its own copy of
every operation whose operands lie in it, remapped: `GateOp`/`UnitaryOp`
qubits go through `global_to_local`; `MeasureOp::clbit` is **never**
remapped (clbits are a global, unpartitioned namespace — a classical bit
written by a qubit in one region can be re-written, later, by a qubit in a
different region; see "Global last-write-wins across regions" below).

**Worked example** (interleaved components, the case
`docs/BUILD_PLAN.md` names explicitly): global qubits `{0,2}` (region A,
`local_to_global=[0,2]`) and `{1,3}` (region B, `local_to_global=[1,3]`).
Global amplitude `a = 0b1011 = 11` (q0=1,q1=1,q2=0,q3=1). Region A gathers
`[0,2]`: local bit0=q0=1, local bit1=q2=0 → `a_A = 0b01 = 1`. Region B
gathers `[1,3]`: local bit0=q1=1, local bit1=q3=1 → `a_B = 0b11 = 3`.
`ψ_global[11] = exp(i·global_phase) · ψ_A[1] · ψ_B[3]`.

**Idle regions.** A component with no `GateOp`/`UnitaryOp` (only possibly
a `MeasureOp`) gets `estimated_state_bytes = 0`; the runtime never
allocates it or samples it — its outcome is statically `|0⟩` with
probability 1 (a static `{1,0}` constant covers the statevector case).

## Exact statevector recombination (`src/runtime/recombine_statevector.*`)

Incremental, one region live at a time: allocate the global buffer
(all-1.0), then for each region in component order, produce its
amplitudes (allocate/execute/release, or the static idle constant) and
multiply them into the global buffer via the gather above, before moving
to the next region. This matches `docs/BUILD_PLAN.md` 3.5's peak-memory
formula ("maximum simultaneously live region... plus the global output
allocation") exactly — verified in
`tests/runtime/runtime_test.cpp::StatevectorPeakBytesIsOneRegionPlusGlobalNotSumOfAllRegions`,
which measures the *real* peak (see "actual_state_bytes is now a genuine
measurement" below), not an assumed one.

**Global phase is applied once, as a scalar over the whole recombined
buffer, after every region has been multiplied in — never folded into any
one region's own initialization.** An earlier version of this code folded
it into "region 0"'s `initialize()` call specifically (region 0 always
contains global qubit 0, by the interaction graph's own numbering
invariant). This is provably correct **only** when region 0 has real
gates; it silently drops the phase whenever region 0 is idle, since the
idle branch never touches `global_phase` at all. This is a real bug that
shipped in this session's own first Factorized implementation and was
caught by `python/tests/test_differential_statevector.py`'s randomized
8-qubit case (fixed seed `20260921`, `num_qubits=8`) — not by a test
written for this specific scenario in advance. The failure showed a
`max abs diff` of `1.3756` with every mismatched amplitude at *identical
magnitude* to the oracle and a consistent phase offset of exactly
`global_phase` (`2.2007` rad) — diagnosed by hand-reconstructing the
circuit's own regions in Python (via Qiskit, as an independent oracle) and
comparing against the raw `menos._core` output before touching any
production code. The fix (multiply the phase into the whole buffer once,
unconditionally) is regression-tested both in C++
(`FactorizedDifferentialTest.GlobalPhaseAppliedEvenWhenRegionZeroIsIdle`)
and Python (`test_factorized_backend.py`'s idle-qubit-0 case).

## Global last-write-wins across regions

`MeasureOp::clbit` is a global namespace: two *different* qubits, in two
*different* regions, can legally write the same clbit at different points
in program order (CLAUDE.md: "a later write to the same bit wins"). The
runtime resolves this **once, globally**, from the unmodified circuit
(`compute_final_writes`, mirroring Phase 1's `Runtime::execute` exactly),
*before* routing the surviving `(qubit, clbit)` pairs to their owning
regions — never per-region, which would let a region's own stale earlier
write survive alongside a later write from a different region.

## Exact factorized counts and `BitString`

### The rejected first draft, and why

A first-draft algorithm sampled each region independently with the
existing sort-uniforms-then-walk-cumulative method (unchanged per
region), then merged the resulting per-region histograms by taking the
union of their cumulative-count boundaries within `[0, shots)`. This
introduces spurious positive correlation between regions.

**Counterexample, verified by hand before any code was written for this
draft:** 2 shots, 2 regions, each a fair coin (2 outcomes, p=0.5). Each
region's own sort-then-walk-cumulative sequence over 2 shots has outcomes
`(0,0)` with p=0.25, `(0,1)` with p=0.5, `(1,1)` with p=0.25 (a
monotonically non-decreasing sequence, since sorted order statistics
assign smaller-index outcomes to smaller-valued draws). Pairing two
independent regions' sequences by shared position index and enumerating
all 3×3 combinations gives `E[count(same-outcome pair, e.g. (0,0) or
(1,1))] = 0.625` and `E[count(different-outcome pair)] = 0.375` per
combined outcome — against a true value of `0.5` for each of the four
equally-likely combined outcomes under genuine independence. The bias
comes from pairing two individually-monotonic (comonotonic-with-
themselves) sequences by shared index — sound for one region's own
marginal histogram, unsound once two regions' sorted sequences are paired
index-by-index.

### The corrected algorithm

For each of `shots` trials, draw exactly one fresh, **unsorted** uniform
per region from that region's own independent Rng stream (seeded via a
per-component sub-seed derived from `common::SplitMix64`), map it through
that region's precomputed `CumulativeDistribution` (binary search over a
cumulative-probability array built once, `O(2^m)`), and OR-combine the
per-region outcomes (valid: each region's outcome bits are disjoint by
construction of the global last-write-wins routing above). This is exact
because, for a fixed shot index, every region's draw comes from an
independent RNG stream and is **not** a sorted order statistic — there is
no rank-pairing to introduce correlation. Verified via chi-square
statistical tests at both the primitive level
(`tests/runtime/combine_counts_test.cpp::TwoIndependentRegionsProduceUniformJointDistribution`)
and end-to-end
(`tests/api/factorized_differential_test.cpp::CombinedCountsAreStatisticallyUniformOverFourOutcomes`),
both against the df=3, p=0.001 chi-square critical value `16.2662`
(independently re-verified this session via
`scipy.stats.chi2.ppf(0.999, 3)`).

`engines::dense::sample()`'s existing sort-then-walk algorithm is
**unchanged** and remains exact for the single-region case (`DirectDense`,
or any one region's own draw when there is no cross-region combination) —
the bias above only arises from *pairing* two independently-sorted
sequences, which the single-region path never does.

### `splitmix64`

**`splitmix64` did not already exist in this codebase**, despite
`CLAUDE.md` ("Derive sub-seeds with splitmix64") and `docs/BUILD_PLAN.md`
§3.4 ("the existing splitmix64 mechanism") referring to it as present —
`grep -ril splitmix` across `src/`, `include/`, `tests/` returned zero
hits before this phase. Implemented from scratch
(`src/common/splitmix64.h`) using Sebastiano Vigna's published,
public-domain construction (golden-gamma increment
`0x9E3779B97F4A7C15`, two xor-multiply-shift mixing rounds with the
standard published constants `0xBF58476D1CE4E5B9`/`0x94D049BB133111EB`) —
implementing a named external algorithm, not inventing mathematics.
Tested for determinism and statistical uniformity/avalanche (chi-square,
16 bins, df=15, critical value `37.6973`, independently verified via
`scipy.stats.chi2.ppf(0.999, 15)`), never against a hand-recalled
"expected value" constant.

### `BitString`

Arbitrary-width, little-endian-by-64-bit-word storage
(`words_[0]` = bits `[0,64)`). Ordering: numeric, lexicographic-by-
most-significant-word (compare the highest-index word first) — matches
Phase 1's `std::map<uint64_t,...>` ascending iteration order.
`to_hex_string()`: lowercase, `0x`-prefixed, no leading-zero padding
beyond a single `"0"` for the zero value — verified byte-identical to
Python's built-in `hex()` on the same represented integer
(`python/tests/test_bitstring_counts.py::test_raw_hex_counts_match_pythons_own_hex`,
including a value spanning past the first 64-bit word).

`engines::dense::sample()` and `Result::counts()` are both
`BitString`-keyed — not conditionally `uint64_t` when a region's *local*
clbit count is small, because `MeasureOp::clbit` is a *global* id: even a
single-qubit factorized region can write global clbit 200. Region-local
clbit *count* is irrelevant; the global clbit *index magnitude* is exactly
what Phase 3 removes the ceiling on. `BitString` is the *one* production
count-key representation (`menos._core.Result.counts` is bound as
`dict[str, int]`, hex keys, converted from `BitString` directly in
`bindings.cpp` — `BitString` itself is not exposed as a Python class, no
Python-side use for it existing).

## Work-unit cost model (`src/cost/cost_model.cpp`)

Per `docs/BUILD_PLAN.md` §3.5, for an `m`-qubit region:
`work = 2^m (init) + Σ_ops 2^(m+k) (k = operand count; MeasureOp
contributes 0) + [2^m if want_statevector] + [2^m if want_counts]`.
Peak bytes: `16 * 2^m`. All arithmetic via
`common::checked_add`/`checked_mul`/`checked_pow2`; the first overflow is
caught internally and converted to `feasible=false` with a documented
reason — never propagated as an exception the caller must catch, never a
silent wraparound.

**Idle components are free** (`estimated_peak_bytes = estimated_work_units
= 0`), matching the runtime's real behavior — charging the generic
per-region formula for an idle component would overstate factorized's
true cost and could wrongly bias the planner away from a genuinely
cheaper factorized plan on circuits with many untouched qubits
(`tests/cost/cost_model_test.cpp::IdleComponentsAreFree`, a 20-qubit
circuit with only 2 qubits ever interacting).

`estimate_factorized`'s total peak is `max` over regions (not sum) plus
the global `16*2^n` buffer when `want_statevector` — matching the
runtime's one-region-at-a-time execution exactly. Its total work adds
`2^n` per region for the incremental recombination multiply pass, when
`want_statevector` (an extrapolation of the spec's phrase "explicit
statevector recombination work," not a formula the spec states
numerically — reasoned from the incremental recombination algorithm's own
shape: it performs exactly one full `2^n`-amplitude pass per region). No
separate count-combination cost term: the number of distinct sampled
outcomes is data-dependent and unknowable at plan time, and is already
upper-bounded by each region's own `2^m` sampling term (the spec
explicitly permits omitting a term that isn't "measurable").

## Planner (`src/plan/planner.cpp`)

`DirectDense` always enumerated first (index 0). `Factorized` enumerated
only when the interaction graph has ≥2 components — "nontrivial" read as
"the graph is not one connected blob," no additional per-component size
threshold (CLAUDE.md explicitly forbids qubit-count/size thresholds as a
factorization criterion). Selection: reject infeasible candidates; among
feasible ones, lowest `estimated_work_units` wins; **exact ties go to the
earlier-enumerated candidate** (`DirectDense`, verified by a hand-derived
tie case, `tests/plan/planner_test.cpp::ExactTieGoesToDirect` — 2 qubits,
one `H` on qubit 1 only, both candidates compute to exactly 16 work units,
brute-force-searched this session, not guessed); both infeasible → one
`ResourceExceeded` naming both reasons.

`Result::actual_state_bytes()` for a factorized run is now a genuine
measurement — the real running peak of (global buffer, if any) + (the
one region live in the current loop iteration), computed as the runtime
executes, not a reuse of the planner's own estimate. This is what makes
`docs/BUILD_PLAN.md` §3.11 item 14 ("runtime peak-memory behavior proving
region state is released before the next region") independently testable:
`tests/runtime/runtime_test.cpp::CountsOnlyPeakBytesIsSingleRegionNotSumOfAllRegions`
and its statevector counterpart construct 3 independent 5-qubit
components (512 bytes each, sum 1536) and assert the reported peak is the
single-region figure, not the sum.

## Benchmark methodology (`docs/BUILD_PLAN.md` §3.9)

Three groups, all run this session (Release build, this machine — see
each benchmark's own printed environment line for exact OS/arch/compiler/
CPU):

1. **Dense gate application**, generic 2-qubit `apply_matrix`, 16–26
   qubits; a data point whose `16*2^n` exceeds this machine's safe
   allocation budget (50% of detected total memory, the same fraction
   `plan::kDefaultMemoryBudgetFraction` uses) is skipped rather than
   allocated until the OS kills the process.
2. **Factorized vs. direct**, the same independent-components circuit
   (fixed component size 4, varying component count) forced through each
   strategy explicitly (bypassing the planner, since this measures
   performance, not planner selection — that's `planner_test.cpp`'s job).
   Measured this session: at 4 components (16 qubits), direct ≈10.1ms vs.
   factorized ≈1.28ms — the gap widens with component count exactly as
   the exponential-vs-linear cost-model reasoning predicts.
3. **Cost-model correlation**: work units vs. measured wall-clock seconds
   across circuit sizes 10–20 qubits (brickwork H+CX), Spearman rank
   correlation. Measured this session: `ρ = 1.0` on this machine.

**This is machine-specific empirical evidence that the cost model ranks
candidates usefully here — it is not a proof the model predicts
wall-clock time universally**, and the benchmark programs themselves print
this caveat. Mathematical correctness (the proof above, and the
differential/statistical tests) is what establishes correctness;
benchmarks establish only that the ranking is empirically useful on the
machine they were run on.

## `Circuit::append_batch` (preliminary step, closes docs/adr/0004's item)

Requested by the owner before Phase 3 proper: a CSR-style batched
named-gate ingestion API, collapsing ~100,000 per-operation
`Circuit::append()` nanobind calls into one `append_batch()` call from
`python/menos/qiskit/convert.py`. Differentially tested against the
equivalent sequence of individual `append()` calls (byte-identical
`Circuit` contents and execution results).

**Honest result**: re-profiling after this change confirmed the targeted
cost (boundary-crossing overhead) dropped to ~2% of `convert_circuit`'s
time, exactly as designed — but the *overall* conversion ratio for the
100k-operation representative circuit did not improve (~15.2% →
~15.8–16.7%, statistically flat), because the newly-dominant cost is
Qiskit's own per-instruction property access (`is_parameterized()`,
`.params`, etc.), not anything on MENOS's side of the boundary. The owner
reviewed this finding and accepted the ~16% ratio as a permanent
limitation rather than trading away a correctness check (which reducing
that cost further would require) for speed. See `docs/adr/0004`'s
"Conversion performance" section for the full measurement.
