# ADR 0006: Cross-engine cost units and the stabilizer tableau

## Status

Accepted.

## Context

Phase 4 adds a second engine (Clifford stabilizer) and the
`SimulationEngine` interface. This requires: (1) a mathematically
precise tableau algorithm, validated against the dense/Qiskit oracle
before it is trusted, exactly like `splitmix64` in Phase 3; (2) one
shared cost-unit definition both engines' `estimate()` derive from,
without benchmark-fitted coefficients.

Every formula below was derived and independently verified during this
session (by direct matrix computation and cross-checked composite-gate
identities), not recalled or assumed, except where explicitly marked as
a cited, independently-validated external algorithm (Aaronson-Gottesman
sign tracking is a published construction, the same epistemic status
`splitmix64` had in Phase 3 -- trusted only jointly with the mandatory
differential test against the dense oracle, `docs/BUILD_PLAN.md` Phase
4's acceptance criterion 1).

## Simplification: no destabilizers, no mid-circuit measurement machinery

The full Aaronson-Gottesman ("Improved Simulation of Stabilizer
Circuits", 2004) tableau carries `2n` rows (`n` destabilizers plus `n`
stabilizers) because its measurement algorithm must support **mid-circuit**
measurement: an outcome collapses the state and subsequent gates can
depend on it, so the destabilizers exist purely as bookkeeping to
re-derive, cheaply, which stabilizer rows must be combined when a later
measurement's outcome is deterministic.

CLAUDE.md's "Terminal measurement only" invariant means MENOS never has
this problem in any phase built so far: a qubit's `MeasureOp` is always
its last operation, and no phase yet has classical control flow. So
every measurement in a job can be resolved **after** every gate has been
applied, from the single final tableau, with no interleaved collapse.
This means:

- Gate application never needs destabilizers: every one of the ten
  Clifford gate-conjugation rules below updates each stabilizer row
  **independently**, using only that row's own bits at the gate's
  qubit(s) -- there is no cross-row interaction during gate application
  (verified by direct derivation for all ten below).
- The measurement algorithm reduces to a **single Gaussian elimination**
  over the `n` final stabilizer generators (see "Sampling" below), not
  the sequential per-qubit CHP algorithm with its random/deterministic
  branching and rowsum bookkeeping.

`Tableau` therefore stores exactly `n` rows (the stabilizer generators),
each `(x ∈ GF(2)^n, z ∈ GF(2)^n, r ∈ GF(2))`, bit-packed into
`ceil(n/64)`-word vectors for `x` and `z` (docs/BUILD_PLAN.md Phase 4:
"bit-packed rows"). If a later phase adds mid-circuit measurement or
classical control flow, destabilizers must be reintroduced -- explicitly
deferred, not built now (CLAUDE.md: "current phase only").

## Representation and the `Y = i·X·Z` convention

Per qubit, `(x,z) = (0,0),(1,0),(0,1),(1,1)` represent `I,X,Z,Y`. `Y` is
defined as `i·X·Z` exactly (CLAUDE.md's Pauli phase convention, matching
Qiskit's own `Pauli`/`SparsePauliOp` convention) -- verified directly:
`X=[[0,1],[1,0]]`, `Z=[[1,0],[0,-1]]`, `XZ=[[0,-1],[1,0]]`,
`i·XZ=[[0,-i],[i,0]]`, which is exactly `matrix_y` in
`src/ir/gate_table.cpp` (`{0, -i, i, 0}`). A row's sign bit `r` is `0`
for `+1`, `1` for `-1`, relative to this exact convention -- not a
generic phase, always real.

## Per-gate conjugation rules (single qubit `a`, or `a,b` for two-qubit
gates), derived by direct matrix computation this session

Each rule updates `(x_a, z_a[, x_b, z_b])` and conditionally flips `r`.
All flips below are evaluated using the **pre-update** bit values.

- **X**: `r ^= z_a`. `(x,z)` unchanged. (X commutes with X, I;
  anticommutes with Z, Y.)
- **Z**: `r ^= x_a`. `(x,z)` unchanged.
- **Y**: `r ^= (x_a ^ z_a)`. `(x,z)` unchanged. (Verified: flips on X and
  Z, not on I or Y itself.)
- **H**: `r ^= (x_a & z_a)`; `swap(x_a, z_a)`. (`HXH=Z`, `HZH=X`,
  `HYH=-Y` -- the `x_a & z_a` term is exactly "was this row a Y before
  the swap".)
- **S**: `r ^= (x_a & z_a)`; `z_a ^= x_a`. Verified directly:
  `SXS† = Y` (no flip), `SZS† = Z` (no flip), `SYS† = -X` (flip) --
  matches `z_a ^= x_a` mapping `X→Y, Z→Z, Y→X` with the sign flipping
  only on the `Y→X` case (`x_a & z_a` true only when the row started as
  `Y`).
- **Sdg**: `r ^= (x_a & (z_a ^ 1))`; `z_a ^= x_a`. Same basis change as
  `S` (`Sdg` and `S` both swap `X↔Y`, fix `Z`), but the sign flips on
  the *other* transition: verified `S†XS = -Y` (flip), `S†ZS = Z` (no
  flip), `S†YS = X` (no flip) -- cross-checked by composing `S` then
  `Sdg` on all four inputs and confirming the identity (net zero flip,
  original state restored).
- **SX**: `r ^= ((x_a ^ 1) & z_a)`; `x_a ^= z_a`. Derived from the full
  `SX · Z · SX†` matrix product (`= -Y`) and `SX · Y · SX† = Z`, `X`
  fixed; verified `SX` then `SXdg` composes to the identity on all four
  inputs.
- **SXdg**: `r ^= (x_a & z_a)`; `x_a ^= z_a`. Derived as the inverse
  conjugation map of `SX` (`SXdg(Y) = -Z`, `SXdg(Z) = Y`, `X` fixed);
  cross-checked the same way as `SX`.
- **CX** (control `a`, target `b`), the standard published formula
  (identical across every independent stabilizer-simulator
  implementation, e.g. Aaronson's own reference CHP code, Qiskit's
  `Clifford`, Stim):
  `r ^= x_a & z_b & (x_b ^ z_a ^ 1)`; `x_b ^= x_a`; `z_a ^= z_b`.
  Independently verified this session by direct Pauli-product
  computation for five diverse cases, including the tightest one:
  `(x_a,z_a,x_b,z_b)=(1,0,0,1)` (`X_a ⊗ Z_b`) gives
  `X_aX_bZ_aZ_b = (X_aZ_a)(X_bZ_b) = (-iY_a)(-iY_b) = -Y_aY_b`,
  matching the formula's predicted `(1,1,1,1)` with a flip exactly.
- **CZ** (symmetric in `a,b`), the standard published formula:
  `r ^= x_a & x_b & (z_a ^ z_b)`; `z_a ^= x_b`; `z_b ^= x_a`. Verified
  `CZ·X_a·CZ = X_a Z_b` (no flip) directly.
- **SWAP** (`a,b`): `swap(x_a,x_b)`; `swap(z_a,z_b)`; never flips `r`
  (a pure relabeling of which qubit each factor acts on).

These ten cover exactly `is_clifford`'s gate set
(`src/ir/gate_table.h`): `X Y Z H S Sdg SX SXdg CX CZ SWAP`.

## `rowsum`: combining two rows (needed only for Gaussian elimination)

Because gate application never combines rows, `rowsum` is needed only
inside the reduction below. `rowsum(h, i)` replaces row `h` with the
product (as Pauli operators) of rows `h` and `i`: `(x_h,z_h) ^= (x_i,z_i)`
per qubit, plus a phase correction, since `Y = i·X·Z` means a literal
`X^{x} Z^{z}` product picks up an implicit `i`-power relative to the
canonical per-qubit type whenever both bits are set.

**Per-qubit phase table** `g(σ1,σ2)` (power of `i` in `σ1·σ2 =
i^g·σ(x1^x2,z1^z2)`), derived this session by direct 2x2 matrix
multiplication for all 16 `(σ1,σ2)` pairs and cross-checked against the
standard cyclic identities `XY=iZ, YZ=iX, ZX=iY`:

| σ1 \ σ2 | I | X | Y | Z |
|---|---|---|---|---|
| I | 0 | 0 | 0 | 0 |
| X | 0 | 0 | 1 | 3 |
| Y | 0 | 3 | 0 | 1 |
| Z | 0 | 1 | 3 | 0 |

(reading `g(X,Y)=1` as `XY=i·Z`, `g(X,Z)=3` as `XZ=i^3·Y=-iY`, etc.)

`rowsum(h,i)`: accumulate `total = 2·r_h + 2·r_i + Σ_q g(σ_h[q], σ_i[q])
(mod 4)`. `total` is always `0` or `2` for the two use cases below (both
only ever combine mutually-commuting Hermitian operators, so the
product is always Hermitian, i.e. a real sign) -- enforced with
`common::ensure` as a self-check, matching "fail loudly." New
`r_h = total / 2`; new `(x_h,z_h) = (x_h ^ x_i, z_h ^ z_i)`.

## Gate application cost

Every gate touches all `n` rows once; each row's update reads/writes
`O(1)` words (the single 64-bit word containing the touched qubit's
bit, for each of the gate's 1 or 2 operand qubits) -- `apply_matrix`-style
per-gate-per-row work, never per-amplitude. Total gate-application cost
for `G` Clifford gates on `n` qubits: `n·G` word-touches.

## Sampling: reduction to an affine subspace (no destabilizers)

**Claim.** For a stabilizer state with generators `s_0..s_{n-1}`
(each `(x_i,z_i,r_i)`), the Z-basis measurement outcome `o ∈ {0,1}^n`
satisfies, for every generator with `x_i = 0` (a pure-Z, "diagonal"
generator): `Σ_q z_i[q]·o[q] ≡ r_i (mod 2)` -- because a diagonal
Pauli's eigenvalue on `|o⟩` is `(-1)^{Σ z_i[q]o[q]}`, and this eigenvalue
is conserved exactly (a commuting observable's eigenvalue does not
change under measurement of the state), so it must equal the
generator's own `(-1)^{r_i}`. Generators with `x_i ≠ 0` impose no direct
per-`o` constraint -- they are exactly the degrees of freedom
("random branches" in the sequential-CHP framing) that make the outcome
non-unique.

**Reduction.** Gauss-Jordan eliminate the `n × n` `x`-part matrix of the
`n` stabilizer rows (row operations = `rowsum`, so signs stay correct
throughout): for each column `col = 0..n-1`, find a not-yet-used row
with `x[col]=1`, make it the pivot for that column, `rowsum` it into
every *other* row (pivot or not) that also has `x[col]=1`, so `col`'s
bit becomes `0` everywhere else. After all columns: exactly `r = rank`
rows retain a nonzero `x`-part (the pivots); the remaining `n-r` rows
have `x` identically zero -- these are the diagonal constraint rows
above (row independence in the elimination is guaranteed since the
original `n` generators are linearly independent in `GF(2)^{2n}`, and
elementary row operations preserve rank/independence).

**Solving the constraints.** The `n-r` diagonal rows' `z`-vectors are
themselves linearly independent in `GF(2)^n` (their `(x,z)` pairs are
independent and `x=0`, so independence reduces to their `z`-parts). A
second Gauss-Jordan elimination, this time on the `(n-r) × n` matrix of
these `z`-vectors augmented with their `r_i` constants, gives a clean
RREF with exactly `n-r` pivot columns and `r` free columns. Standard
back-substitution gives: a **particular solution** `base` (every free
variable `0`), and, for each free column `f_j`, a **generator** `g_j`
(set `o[f_j]=1`, every other free variable `0`, solve the pivot
variables) -- `r` vectors spanning the null space. Every point of the
affine solution set is `base ⊕ (⊕ of a subset of the g_j)`, and every
subset gives a distinct point (the `g_j` are independent), so this is
exactly the base-plus-generator-basis the spec describes, and `r` is
the reported affine dimension (`docs/BUILD_PLAN.md` Phase 4's acceptance
criterion 1: "the affine dimension `r`... dense probabilities are each
0 or `2^-r`").

**Projection and sampling.** For a queried subset of qubits (a region's
`measure_targets`), restrict `base` and each `g_j` to those qubit
positions. "The image of a uniform distribution under a linear map over
GF(2) is uniform over the image" (spec's own justification): each shot
draws `r` independent random bits (from the region's `common::Rng`,
whole 64-bit words, `ceil(r/64)` words per shot, individual bits read
off each word -- never `std::` distributions) and XORs in the projected
`g_j` for every bit set, added to the projected `base`.

**Why this deviates from the "process qubits sequentially, capture
pivot rows" recommendation, and why that's permitted.** The spec offers
this Gaussian-elimination reduction as the concrete mechanism and
explicitly permits "any other method... if it matches the dense oracle."
The sequential-CHP alternative requires destabilizer bookkeeping this
codebase has no other use for (see "no destabilizers" above) and a
retroactive counterfactual re-run to correctly propagate a flipped
random choice to *later* qubits' deterministic outcomes -- meaningfully
more moving parts for a terminal-measurement-only codebase with no other
consumer of destabilizers. The Gaussian-elimination approach reduces
the whole problem to standard, independently-checkable `GF(2)` linear
algebra and produces the *same object* (base + generator basis) that
feeds the *same* recommended per-shot XOR-and-project sampling step, so
it satisfies the acceptance criteria's sampling behavior (chi-square
against `2^-r`, same-seed-same-counts) by the same argument the spec
gives for its own recommended method.

## Sampling and reduction cost

Elimination: two Gauss-Jordan passes, each bounded by `n` columns/rows
times up to `n` candidate rows times a `rowsum` of `ceil(n/64)` words:
`2 · n² · ceil(n/64)` word operations (upper bound, not tightened
further -- CLAUDE.md forbids benchmark-fitted coefficients, not a loose
but honest upper bound).

`SimulationEngine::estimate()` (like `DenseEngine`'s, unchanged from
Phase 3) has no `shots` parameter -- the cost model has never scaled the
"counts requested" term by shot count (Phase 3's own `region_cost`
charges a single flat `2^m` for `want_counts`, regardless of whether
`shots` is 1 or 1,000,000), so the stabilizer engine's `want_counts`
term is, by the same convention, one representative sampling pass: `n ·
ceil(n/64)` word operations (worst case `r ≤ n` generators, each
`ceil(n/64)` words, XORed once) -- not multiplied by the actual shot
count, matching dense's own flat per-output-pass charge exactly in
spirit. The *actual* runtime cost of sampling is genuinely
`O(shots · r · ceil(k/64))` (see "Projection and sampling" above); the
gap between that and this flat estimate is the same kind of approximation
Phase 3's dense cost model already accepts for its own `sample()` call
(also genuinely `O(shots log shots + 2^m)`, not `O(2^m)`), not a new
looseness Phase 4 introduces.

## `estimate()` and peak bytes

```
words = ceil(num_qubits / 64)
peak_bytes = num_qubits * 2 * words * 8        (x-words + z-words per row)
           + words * 8                          (packed sign bits)
work = num_qubits                                (row initialization)
     + num_gates * num_qubits                    (gate application, all Clifford)
     + [want_counts] * (2 * num_qubits^2 * words          (elimination)
                        + num_qubits * words)             (one representative sampling pass)
```
All via `common::checked_add`/`checked_mul` (no `checked_pow2` needed --
every term is polynomial in `num_qubits`, never exponential, which is
exactly why the stabilizer engine reaches 500 qubits where dense cannot:
`num_qubits=500` gives `peak_bytes` on the order of tens of kilobytes,
not `16 * 2^500` bytes). `want_statevector` makes the estimate
infeasible unconditionally (`capabilities().statevector == false`),
with the reason string containing the literal phrase "only the dense
engine can produce a statevector" (`docs/BUILD_PLAN.md` Phase 4
acceptance criterion 2's required message). A non-Clifford `GateOp` or
any `UnitaryOp` makes the estimate infeasible, naming the first such
operation's index and name (criterion 3/4).

## Dense engine's cost unit, restated for comparability

Unchanged from `docs/adr/0005` (relocated verbatim into
`DenseEngine::estimate()`, Phase 4): `work = 2^m (init) + Σ_ops
2^(m+k) + [2^m if want_statevector] + [2^m if want_counts]`, one unit
per complex multiply-add. The two engines' units are **not claimed to be
wall-clock-equivalent** -- a stabilizer word-touch and a dense complex
multiply-add are different operations on different hardware paths.
"Shared cost unit" here means: both are analytic, checked-arithmetic,
integer functions of the same *structural* inputs (qubit count,
per-operation counts, requested outputs), so the planner can rank
*candidates for the same job* consistently (lower `estimated_work_units`
among feasible candidates wins, `docs/BUILD_PLAN.md` section 4) --
exactly Phase 3's own definition of the cost unit's purpose, extended to
a second engine.

## Benchmark evidence (predicted vs. measured ratio, report only)

See `benchmarks/stabilizer_cost_correlation_benchmark.cpp`, added
alongside this ADR: work units vs. measured wall-clock seconds for both
engines on overlapping all-Clifford circuits, same Spearman-rank-
correlation methodology as `docs/adr/0005`'s existing dense-only
benchmark. Machine-specific findings recorded in this session's final
report, not restated here (this ADR fixes the *method*, not one
machine's numbers).

## Validation

Every rule above is validated, before being trusted, by
`tests/engines/stabilizer/tableau_test.cpp` (direct, hand-computed
single/two-row cases for each of the ten gates, including the sign-bug-
prone `Y, S, Sdg, SX, SXdg` and X-basis (`H` before `Measure`) cases the
spec calls out) and by `tests/engines/stabilizer/stabilizer_engine_test.cpp`
(random Clifford circuits, 1-12 qubits, differentially compared against
`qiskit.quantum_info.Statevector` per `docs/BUILD_PLAN.md` Phase 4's
acceptance criterion 1 -- exact affine dimension `r`, exact nonzero-
outcome set, and chi-square sampled counts).
