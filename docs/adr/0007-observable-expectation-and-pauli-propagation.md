# ADR 0007: Observable expectation and Pauli propagation

## Status

Accepted (C++ engines, planner, runtime). The Qiskit `EstimatorV2`
adapter (`menos.qiskit`) is **not** covered by this ADR and is not yet
built -- see the end-of-task report for Phase 5's remaining scope.

## Context

Phase 5 adds a third exact output kind, `expectation` (a weighted sum of
Pauli-string observables), computed by whichever of three routes is
cheapest for a region: dense (full statevector formula), stabilizer
(GF(2) tableau reduction), or a new third engine, Pauli propagation
(Heisenberg-picture backward conjugation, no state ever materialized).
Every formula below was derived and independently verified this
session, by direct matrix computation against the exact matrices in
`src/ir/gate_table.cpp` -- not recalled, not assumed, and, for the
Pauli-propagation gate rules, re-verified a second time after an error
was caught in the phase specification itself (see "The corrected
term-growth bound" below).

## `PauliString`/`Observable` representation

```cpp
struct PauliString { std::uint32_t num_qubits; std::vector<uint64_t> x_words, z_words; };
struct Observable { std::vector<std::pair<PauliString, std::complex<double>>> terms; };
```

Per qubit `j`, `(x,z) = (0,0),(1,0),(0,1),(1,1)` represent `I,X,Z,Y`,
`Y = i·X·Z` exactly -- the identical convention `docs/adr/0006` already
verified against `src/ir/gate_table.cpp`'s `matrix_y` and reused
unchanged here (`common::PauliRow`, relocated from `Tableau`'s own
private row type, is the single shared representation both the tableau
and the Pauli propagation engine now use).

## Dense expectation: single-pass formula

For one term `(x,z)`, per-qubit conjugation gives
`σ_j|k_j⟩ = i^{[σ_j=Y]}·(-1)^{z_j k_j}·|k_j ⊕ x_j⟩`, so applying every
qubit's operator to a full basis state `|k⟩` gives
`P|k⟩ = i^{popcount(x&z)}·(-1)^{popcount(z & k)}·|k ⊕ x⟩` (the
`popcount(x&z)` count of `Y`s contributes one factor of `i` each, and
`i^{count}` collapses to a single power-of-`i` multiplier since every
`Y` contributes the same phase). Therefore:

```
<psi|P|psi> = i^popcount(x&z) * sum_m conj(psi[m]) * psi[m^x] * (-1)^popcount(z & (m^x))
```

one pass over the state, no allocation
(`src/engines/dense/dense_engine.cpp::expectation_of_term`). Verified
by hand for `Z` and `X` on one qubit (textbook results), and by the
required differential test against `qiskit.quantum_info.Statevector`
fixtures, including explicit `Y`-on-1/2/3-qubit cases (Phase 5
acceptance criterion 1).

## Stabilizer expectation: GF(2) elimination with a combination mask

**Claim.** The `n` stabilizer generators span an `n`-dimensional
Lagrangian (self-dual) subspace of `GF(2)^{2n}` under the symplectic
form `⟨(x,z),(x',z')⟩ = x·z' + x'·z (mod 2)` (this is exactly the
"every generator commutes with every other generator" condition,
restated as a bilinear form). A query `PauliString` `q` commutes with
every generator iff `⟨q, s_i⟩ = 0` for all `i`, i.e. iff `q` is
symplectic-orthogonal to the whole `n`-dimensional subspace -- and
because that subspace is self-dual (Lagrangian) in the `2n`-dimensional
symplectic space, its orthogonal complement **is itself**, so `q`
commutes with every generator iff `q` lies in the span of the
generators. This is the same fact Aaronson-Gottesman's own group-theory
argument for stabilizer states relies on, restated in strictly linear
terms so it can be checked by ordinary GF(2) elimination.

**Algorithm.** Gauss-Jordan eliminate the `n` generators' full `(x,z)`
vectors (not just `x`, unlike the *sampling* reduction in
`docs/adr/0006`, which only needs the `x`-part), augmenting the
elimination with an `n`-bit "combination mask" per row (the standard
change-of-basis bookkeeping: row `h`'s mask records which original
generators combine, via `rowsum`, to produce row `h`'s current
content). To evaluate a query: reduce it against the same pivots. A
nonzero residual after full reduction means the query does not lie in
the generators' span, i.e. it anticommutes with some generator --
expectation exactly `0`. A zero residual means the query equals the
`rowsum` product of exactly the original generators named by the
accumulated combination mask, and that product's sign (real, by
`rowsum`'s own invariant, restated from `docs/adr/0006`) is the
expectation.

`Tableau::expectation_sign(const PauliString&) -> std::optional<bool>`
implements this: `std::nullopt` for anticommuting (expectation `0`);
otherwise the sign. Validated against the full `{-1,0,+1}` range by a
random-Clifford differential test against the dense oracle, plus a
hand-derived Bell-pair case exercising the reconstructed-product path
specifically (`X₀X₁=+1, Z₀Z₁=+1, Y₀Y₁=-1`).

## Light-cone reduction

A new pass, run once per job, unconditionally, before interaction-graph
analysis (`docs/BUILD_PLAN.md` Phase 5). Support is: every qubit, for a
statevector request (a lossless no-op there -- every qubit's amplitude
is observed); every measured qubit, for a counts request; the union of
every observable term's support (`x`-bit or `z`-bit set), for an
expectation request; the union of whichever apply, when a job requests
more than one output kind at once. `analysis::compute_light_cone` walks
`circuit.operations()` in reverse; an operation is kept iff any of its
touched qubits (including a `MeasureOp`'s own qubit) is already in the
cone, and it then adds all its own qubits to the cone. Exact and
lossless by construction: an operation outside the union of every
requested output's causal past cannot affect that output.

The job's own `Observable` is defined over the *original* circuit's
qubits; `plan_job` remaps it once, up front, via the light-cone pass's
own `reduced_to_original` permutation (`common::restrict_observable`,
reused here as a qubit-remapping tool, not just a region-restriction
tool -- the identity `global_to_local` case is exactly a permutation).
This remap is checked to preserve every term (`common::ensure`): by
construction of the support computation, light-cone reduction never
drops a qubit any observable term needs.

**Cost of the pass**: linear in circuit size (one reverse pass over
operations, each processed in `O(1)` amortized), paid once per job
regardless of which engine or strategy is chosen afterward -- not
separately charged in any candidate's own `estimated_work_units`,
matching `docs/BUILD_PLAN.md`'s "linear in circuit size, paid once
regardless of which engine is chosen afterward."

## Pauli propagation engine

### The corrected term-growth bound

`docs/BUILD_PLAN.md`'s original Phase 5 text claimed every one of
`RZ`/`RX`/`RY`/`P`/`U` "splits a non-commuting term into two weighted
terms." This is true for the four single-axis rotations but **false in
general for `U(theta, phi, lambda)`**: `U` has no fixed Pauli axis
except identity (its own decomposition needs up to two of the three
generic Pauli rotations, `RZ`/`RY`/`RZ`, composed), so conjugating a
generic input Pauli by `U` can produce a linear combination of all
three basis Paulis `{X, Y, Z}`, not merely two. This was caught by
direct derivation before any propagation code was written, confirmed by
the owner, and corrected as follows (`docs/BUILD_PLAN.md`'s Pauli
propagation bullet was updated to match):

- **RZ, RX, RY, P**: fix one Pauli axis exactly (the rotation's own
  axis) and mix the other two -- **at most 2** output terms.
- **U(theta, phi, lambda)**: fixes only identity; a non-identity input
  can produce all three of `{X, Y, Z}` in the output -- **at most 3**
  output terms.
- Corrected worst-case bound: `t_final <= t0 * 2^g * 3^h`, where `t0` is
  the observable's own starting term count, `g` is the number of
  `RZ`/`RX`/`RY`/`P` gates in the circuit, and `h` is the number of `U`
  gates -- **not** `t0 * 2^(g+h)`. Computed via `common::checked_mul` in
  a loop (base-2 for `g` iterations, then base-3 for `h`), never
  truncated on overflow: an unrepresentable bound makes the whole job
  infeasible before propagation ever starts
  (`PauliPropagationEngine::estimate()`), exactly like every other
  engine's checked-arithmetic infeasibility path.

### Propagation direction: Heisenberg picture, backward through the circuit

Computing `<0|U^dagger P U|0>` for the circuit's own unitary `U`
requires processing gates in **reverse program order**, replacing the
tracked term `P` with `G^dagger · P · G` at each step -- the opposite
convention from `Tableau`'s forward state-evolution rule
(`G · P · G^dagger`, docs/adr/0006). Consequences, each verified
directly against the installed gate matrices in
`src/ir/gate_table.cpp`:

- Every **self-adjoint** gate (`X Y Z H CX CZ SWAP`, `G = G^dagger`) is
  unaffected: backward conjugation is identical to `Tableau`'s forward
  rule, so `PauliPropagationEngine` reuses `common::apply_x/y/z/h/cx/cz/swap`
  unchanged.
- **`S` and `Sdg` are adjoints of each other, not of themselves**, so
  backward through a circuit `S` gate must call `apply_sdg` (and
  backward through `Sdg` must call `apply_s`): `apply_sdg` already
  computes `Sdg · P · Sdg^dagger = S^dagger · P · (S^dagger)^dagger =
  S^dagger · P · S`, which is exactly the backward rule for a circuit
  `S` gate. The same argument swaps `SX`/`SXdg`. Pinned by
  `tests/engines/pauli/pauli_propagation_engine_test.cpp`'s
  `HThenSThenHMatchesDirectMatrixComputation` (a circuit engineered so
  the wrong choice at the `S` step flips the final sign, `+1` vs.
  `-1`, not merely a magnitude difference) and
  `SxThenHThenSxThenHMatchesDirectMatrixComputation`, plus a random
  differential test whose Clifford gate pool includes `SX`/`SXdg`.

### Single-axis rotation formulas (`G^dagger P G` computed directly from `matrix_rz`/`matrix_p`/`matrix_rx`/`matrix_ry`)

```
RZ(theta): X -> cos(theta)X - sin(theta)Y   Y -> sin(theta)X + cos(theta)Y   Z unchanged
RX(theta): Y -> cos(theta)Y - sin(theta)Z   Z -> sin(theta)Y + cos(theta)Z   X unchanged
RY(theta): X -> cos(theta)X + sin(theta)Z   Z -> -sin(theta)X + cos(theta)Z  Y unchanged
P(lambda): identical to RZ(lambda) -- P and RZ differ only by a global
           phase (P(l) = e^{il/2}*RZ(l) exactly), which cancels exactly
           under G^dagger(.)G conjugation.
```

Each derived by explicitly multiplying `G^dagger`, the Pauli matrix, and
`G` (`matrix_rz = diag(e^{-i*theta/2}, e^{i*theta/2})`, etc.), with no
angle negation needed -- these formulas already are the backward
(`G^dagger P G`) rule, computed directly with the circuit's own angle.

### General `U(theta, phi, lambda)` formulas

Derived directly from `matrix_u`'s four entries
(`{c, -e^{il}s, e^{ip}s, e^{i(p+l)}c}`, `c=cos(theta/2), s=sin(theta/2)`)
by explicit `U^dagger · sigma · U` matrix multiplication, each output
written as `a·X + b·Y + c'·Z`:

```
input X: a = cos(theta)cos(phi)cos(lambda) - sin(phi)sin(lambda)
         b = -sin(phi)cos(lambda) - cos(theta)cos(phi)sin(lambda)
         c' = sin(theta)cos(phi)
input Z: a = -sin(theta)cos(lambda)
         b = sin(theta)sin(lambda)
         c' = cos(theta)
input Y: a = cos(phi)sin(lambda) + cos(theta)sin(phi)cos(lambda)
         b = cos(phi)cos(lambda) - cos(theta)sin(phi)sin(lambda)
         c' = sin(theta)sin(phi)
```

Cross-checked two ways: (1) `U(theta,0,0) == RY(theta)` and
`U(0,phi,lambda) ~ RZ(phi+lambda)` (up to the global phase that cancels
under conjugation) both reduce these formulas to the single-axis
formulas above exactly; (2) a direct, from-scratch re-derivation
(matrix multiplication of `U^dagger`, `X`/`Y`/`Z`, `U`) done a second
time independently of the first, landing on the identical `a, b, c'`
expressions. `tests/engines/pauli/pauli_propagation_engine_test.cpp`'s
`USingleQubit{X,Y,Z}ObservableMatchesDirectMatrixComputation` check
these against `direct_expectation`, a from-scratch 2x2 matrix
computation independent of `common::PauliRow`/`apply_*` entirely, at
non-special parameter values (`theta=1.1, phi=0.7, lambda=-0.4`, chosen
so every trig factor above is nonzero) -- the owner's explicit
requirement that the convention be "verified directly against the
actual matrix implementation being used," not derived once and trusted.
`USpecialCaseThetaOnlyMatchesRyDirectly` and
`USpecialCaseCasePhiLambdaOnlyMatchesRzUpToGlobalPhase` are confirming
special-case tests only, per the owner's explicit instruction not to
use a special case to justify a general 2-term bound for `U`.

A term whose intermediate `a`/`b`/`c'` coefficient is exactly `0.0`
(floating point) is pruned from the output (`split_u`'s
`if (a != 0.0)` guards) as a harmless optimization, not a correctness
requirement -- `estimate()`'s worst-case bound always assumes the full
branching factor regardless of whether a specific term collapses
smaller at runtime, so pruning never invalidates the pre-checked
feasibility bound.

`tests/engines/pauli/pauli_propagation_engine_test.cpp`'s
`TwoIndependentUGatesMatchesDenseOracleOnXxX` forces genuine `3x3=9`-way
term growth: two `U` gates on two *different*, non-interacting qubits
(same-qubit composition of two `U`s collapses back to a single `<=3`-term
rotation, so it cannot demonstrate the full branching factor), with an
`X⊗X` observable, checked against the dense oracle.

### Unsupported opcodes

`T`, `Tdg`, `CP`, `CCX` and `Unitary` are not supported this phase
(`estimate()` infeasible, naming the first such operation's index and
name) -- their branching factor under conjugation is not derived here.
The informal "Clifford+T" acceptance case is covered exactly, since
`T = P(pi/4)` as matrices (`src/ir/gate_table.cpp`), so a `P(pi/4)` gate
serves as `T` for every Pauli-propagation test that needs it.

### A fully Clifford region is a hard routing exclusion, not a cost preference

`docs/BUILD_PLAN.md`'s planner test requirement states "a Clifford
region routes to `DirectStabilizer` not `DirectPauli` or dense." Under
the honest cost formulas above, this does **not** fall out of cost
comparison alone: propagating a single observable term backward through
a Clifford circuit costs `O(num_gates)` (one row touched per gate),
strictly less than the stabilizer tableau's `O(num_gates * num_qubits)`
gate-application cost plus its `O(num_qubits^2)` per-term elimination
(no cross-term caching yet) -- so for a small number of observable
terms, `DirectPauli`'s own estimate is *cheaper* than
`DirectStabilizer`'s for every Clifford circuit, regardless of size.
This was caught by an explicit planner test
(`PlanJobExpectationTest.AllCliffordCircuitRoutesToStabilizerNotPauliOrDense`)
before being silently left as "whichever the cost model happens to
prefer." Per the owner's explicit direction, this is a **hard planner
eligibility constraint**, applied before cost comparison, not a
cost-model tiebreak: `PauliPropagationEngine::estimate()` returns
infeasible whenever every operation in the request is a named Clifford
gate (`is_fully_clifford`, vacuously true for zero `GateOp`s too),
naming the reason explicitly. This keeps the rule inside the engine's
own `estimate()` -- architecturally the same kind of eligibility check
`estimate()` already performs for `want_statevector`/`want_counts` and
unsupported opcodes -- so it applies uniformly through the one
`SimulationEngine` interface the planner and cost model see, for both
`Direct*` and `Factorized` routing, with no special-casing in
`plan_job` itself.

### A latent runtime bug light-cone reduction exposed

Adding a genuine light-cone-reduced `Direct*`-strategy acceptance test
(`ExplainTest.ShowsLightConeReductionAndExpectationValue`) surfaced a
real bug, unrelated to the routing question above:
`runtime::execute_direct` built its `EngineExecutionRequest::num_qubits`
from `job_spec.circuit.num_qubits()` (the **original**, un-reduced
circuit) and its `measure_targets` from the original circuit's qubit
ids, unremapped -- both wrong whenever light-cone reduction actually
drops a qubit and the winning strategy is `DirectDense`/
`DirectStabilizer`/`DirectPauli` (not `Factorized`, which already used
each region's own `local_to_global.size()` correctly). No test before
this one exercised that combination: every existing `want_statevector`
request forces light-cone's support to every qubit (no reduction, see
`compute_light_cone_support`), and every existing counts/expectation
test's support happened to already cover every qubit the winning
strategy's region touched. Fixed by reading `num_qubits` from
`region.local_to_global.size()` and remapping each measured qubit
through `region.global_to_local`, matching `execute_factorized`'s
existing (already-correct) pattern.

### Cost formula

```
words = ceil(num_qubits / 64)
branching = 2^g * 3^h                          (checked, per job above)
max_terms = t0 * branching
peak_bytes = max_terms * (2 * words * 8 + 16)  (row bits + complex128 weight)
work = max_terms * num_gates                   (loose upper bound: assumes every
                                                 term touched at every gate, not
                                                 just after it branches)
```

An honest, loose upper bound (not benchmark-fitted), matching
`docs/adr/0006`'s own stated philosophy for the stabilizer engine's
elimination cost.

## `combine_expectation`: factorized recombination

For a tensor-product state `|psi⟩ = |psi_A⟩ ⊗ |psi_B⟩` and a term whose
support spans both regions, `<psi|P_A⊗P_B|psi⟩ = <psi_A|P_A|psi_A⟩ ·
<psi_B|P_B|psi_B⟩` (the mixed-product property -- the same argument
`docs/adr/0005` used for statevector recombination, applied to an
operator instead of a state). Consequently `EngineExecutionResult`
carries `expectation_terms`, a **raw**, per-term `<psi|P|psi>` vector
(coefficient *not* applied, in `request.observable`'s own term order) --
not a single pre-summed, pre-weighted value -- specifically so a
Factorized plan can take the product, across regions, of each region's
own raw value for a given term index before multiplying by that term's
(shared, unrestricted) coefficient and summing over terms
(`src/runtime/combine_expectation.h`). A `DirectDense`/`DirectStabilizer`/
`DirectPauli` plan's single sole region is the degenerate one-region
case of the same formula. An idle region (no gate ever touches it)
contributes, per term, `1.0` if the term's restriction to that region
is the identity (no `X`/`Y` component) and `0.0` otherwise -- computed
directly from the region's own restricted `PauliString` bits, with no
engine invoked (`src/runtime/runtime.cpp::idle_region_expectation_terms`),
the same "`x != 0` is orthogonal to `|0⟩`" fact the Pauli propagation
engine's own final evaluation step already relies on.

## Truncated Pauli propagation: deferred

A rigorous error bound for truncation *is* derivable (triangle
inequality: dropping a set of terms bounds the total error by the L1
norm of the dropped coefficients, since every Pauli string's own
expectation lies in `[-1,1]`) -- but *which* terms to drop is a
threshold policy with no value given by the phase specification, and
CLAUDE.md forbids inventing numerical thresholds without a reviewed
design document. Exact mode only is shipped this phase, per
`docs/BUILD_PLAN.md`'s own explicit permission ("If the ADR cannot
derive a rigorous bound for truncated mode, ship exact mode only ...
with the ADR stating why" -- here the bound itself is derivable, but the
threshold policy is not, which is the same "stop and ask" situation
CLAUDE.md's working agreement calls for).

## Validation

`tests/common/pauli_row_test.cpp` (the relocated `PauliRow` primitive),
`tests/engines/stabilizer/tableau_test.cpp` (expectation_sign, including
the Bell-pair reconstructed-product case), `tests/engines/dense/`
(differential against the Qiskit fixture, extended with random
`SparsePauliOp` terms and explicit `Y`-on-1/2/3-qubit cases),
`tests/engines/pauli/pauli_propagation_engine_test.cpp` (direct-matrix
conjugation checks for `U`, special-case reductions, the `S`/`Sdg` and
`SX`/`SXdg` direction pins, the 9-way term-growth case, a broad random
differential test against the dense oracle spanning Clifford, `U` and
`RX`/`RY`/`RZ`/`P` gates together, and the term-count-bound-exceeded
infeasibility test), `tests/analysis/light_cone_test.cpp`, and
`tests/plan/planner_test.cpp` together cover Phase 5's acceptance
criteria 1-6.
