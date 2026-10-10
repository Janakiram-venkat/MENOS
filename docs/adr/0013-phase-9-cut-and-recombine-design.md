# ADR 0013: Phase 9 — cut and recombine, cutting-mathematics design

## Status

**Approved by the owner and implemented.** P1-P4 and A1-A3 were locked by the
owner on 2026-09-29; AC-0 was then run and passed; the implementation followed.
Section 13 records what was measured and what was actually built, kept separate
from the design as it stood before either.

The design below is preserved as written, so the difference between what was
assumed and what was found stays visible.

Three kinds of statement are kept visibly separate throughout:

- **[SOURCE]** — established mathematics, with citation and equation number.
- **[MENOS]** — a design decision made here, which the owner is being asked
  to approve.
- **[ASSUMPTION]** / **[OPEN]** — something not yet established, flagged
  rather than resolved.

---

## 0. Why MENOS's situation differs from the literature's

Nearly all circuit-cutting literature is motivated by running a large circuit
on a **small quantum computer**: the fragments are executed on hardware, so
the reconstruction must be estimated from shots, and the central quantity is
a *sampling overhead*.

MENOS is a **classical simulator**. Its fragments are simulated, not sampled,
so it can evaluate every branch of the decomposition deterministically and
sum them exactly. This changes which quantity matters:

- The literature's central number is `gamma`, the 1-norm of the
  quasiprobability decomposition, because shots scale as `gamma^2/eps^2`.
- MENOS's central number is the **branch count** — how many fragment
  evaluations the exact sum requires.

These are different numbers and must never be conflated. For a wire cut the
1-norm is 4 while the term count is 8 (§2.1). Keeping them apart is the same
discipline `CLAUDE.md`'s testing rules now demand after Phase 8, where a
metric's arithmetic was validated while the question of what it bounded went
unasked.

The value proposition also differs. Cutting does not give MENOS access to a
bigger machine; it trades **memory for time**, letting peak memory follow the
largest fragment instead of the whole circuit, at the cost of an exponential
number of fragment evaluations.

---

## 1. Cutting methods considered

### 1.1 Wire cutting (space-like cut)

Cut a qubit's world line at an instant, replacing the identity channel on
that wire with a quasiprobability sum of measure-and-prepare pairs.
**[SOURCE]** Peng, Harrow, Ozols & Wu, *Simulating Large Quantum Circuits on
a Small Quantum Computer*, Phys. Rev. Lett. **125**, 150504 (2020),
arXiv:1904.00102, Lemma 1 and Eq. (2).

### 1.2 Gate cutting (quasiprobability decomposition of a two-qubit gate)

Replace a two-qubit gate by a quasiprobability sum of products of local
operations, so the two qubits need never interact in one fragment.
**[SOURCE]** Mitarai & Fujii, *Constructing a virtual two-qubit gate by
sampling single-qubit operations*, New J. Phys. **23**, 023021 (2021),
arXiv:1909.07534.

### 1.3 Exact boundary contraction

Contract the boundary index between two regions exactly, with no
quasiprobability decomposition: the fragments share a summed index of
dimension `2^b` for a `b`-qubit boundary. This is what a tensor-network
contraction already does internally, and is the mathematics behind Phase 3's
existing tensor-product recombination in the special case `b = 0`.

### 1.4 Comparison

| Axis | Wire cutting | Gate cutting | Exact boundary contraction |
|---|---|---|---|
| Exact reconstruction possible | Yes, by summing all branches | Yes, by summing all branches | Yes, inherently |
| Branch/term count per cut | 8 **[SOURCE]** PRL Eq. (2) | 6 single-qubit operations for CNOT **[SOURCE]** arXiv:1909.07534 | none; one contraction over `2^b` |
| 1-norm `gamma` per cut | 4 (LO), 3 (LOCC) **[SOURCE]** arXiv:2302.03366 Props 4.1, 4.2 | 3 for CNOT, 7 for SWAP **[SOURCE]** arXiv:2312.11638 | not applicable |
| Sampling overhead if sampled | `gamma^{2K}`, i.e. `16^K` (LO) | `9^k` for k CNOT cuts | none |
| Reconstruction complexity | `8^K` signed scalar products | product of per-gate term counts | `2^b` summation |
| Variance if sampled | set by `gamma` (§3) | set by `gamma` (§3) | none |
| Classical cost driver | fragment count × fragment cost | same | boundary dimension `2^b` |
| Engine compatibility | needs Pauli-eigenstate init + Pauli expectation | same, plus mid-circuit local operations | needs an engine that can hold an open boundary index |
| Applies to | any wire, anywhere | only two-qubit gates | only where the boundary is small |

### 1.5 Recommendation — **PROPOSED (P1), not approved**

**Wire cutting, with exact enumeration, for Phase 9.** Reasons, in order of
weight:

1. **Generality.** A wire cut applies at any point on any qubit; a gate cut
   applies only where a two-qubit gate sits. The planner needs freedom to
   place cuts where the interaction graph is thin, not only where a gate
   happens to be.
2. **It fits MENOS's existing machinery.** What crosses a wire cut is a Pauli
   observable on one side and a Pauli eigenstate on the other (§2.1). MENOS
   already restricts observables per region (`common::restrict_observable`,
   Phase 5) and already has engines that compute Pauli expectations.
3. **Exact boundary contraction is not a competitor here.** For `b = 0` it is
   already implemented as Phase 3 factorization; for `b > 0` it requires an
   engine that can carry an open index, which is the tensor-network engine's
   job, not a cutting mechanism.

Gate cutting is **not** rejected on the merits — for a circuit whose only
cross-partition interaction is a handful of CNOTs, `gamma(CNOT) = 3` beats a
wire cut's 4, and the branch count is lower. It is deferred because doing
both at once doubles the derivation, the estimator and the test surface for a
phase that has not yet demonstrated its first motivating case (§10.1).
**[OPEN]** Whether to add gate cutting in a later phase should be reopened
once wire cutting has a measured workload.

---

## 2. Reconstruction mathematics

### 2.1 Wire cutting, derived

**[SOURCE]** For any 2×2 matrix `A`, the Pauli-basis expansion
(arXiv:1904.00102, proof of Lemma 1, Eqs. 12–19):

```
A = [ tr(A·I)·I + tr(A·X)·X + tr(A·Y)·Y + tr(A·Z)·Z ] / 2
```

The factor `1/2` is `1/dim`, from `tr(sigma_i sigma_j) = 2 delta_ij`. It is
not a convention: dropping it scales every reconstructed value by `2^K`.

Applying this to the single-qubit reduced operator flowing along the cut wire
and expanding each Pauli on the *preparation* side into its eigenprojectors
(`I = |0><0| + |1><1|`, `X = |+><+| − |−><−|`, `Y = |+i><+i| − |−i><−i|`,
`Z = |0><0| − |1><1|`) gives the eight measure-and-prepare pairs. **[SOURCE]**
The table below is as given in the proof:

| i | Observable `O_i` (measured on the upstream fragment) | Prepared state `rho_i` (downstream fragment) | `c_i` |
|---|---|---|---|
| 1 | `I` | `\|0><0\|` | +1/2 |
| 2 | `I` | `\|1><1\|` | +1/2 |
| 3 | `X` | `\|+><+\|` | +1/2 |
| 4 | `X` | `\|−><−\|` | −1/2 |
| 5 | `Y` | `\|+i><+i\|` | +1/2 |
| 6 | `Y` | `\|−i><−i\|` | −1/2 |
| 7 | `Z` | `\|0><0\|` | +1/2 |
| 8 | `Z` | `\|1><1\|` | −1/2 |

so that the identity channel on the cut wire is

```
id(rho)  =  sum_{i=1}^{8}  c_i · tr(O_i · rho) · rho_i                    (2.1)
```

**[SOURCE]** arXiv:1904.00102 Eq. (2) states the resulting network identity
as `T(G,A) = sum_{i=1}^{8} c_i T(G',A_i)` with `c_i ∈ {−1/2, +1/2}`.

**Single-cut expectation reconstruction.** Let the cut split the circuit into
an upstream fragment `A` (containing the cut wire's origin) and a downstream
fragment `B`. Let the target observable be a Pauli string `P`, and let `P_A`,
`P_B` be its restrictions to each fragment's qubits. Then

```
<P>  =  sum_{i=1}^{8}  c_i · <P_A ⊗ O_i>_A · <P_B>_{B, cut wire prepared in rho_i}
                                                                          (2.2)
```

Each factor is an ordinary Pauli expectation on a smaller circuit. Note what
fragment `A` computes: **not** `O_i` alone, but the joint Pauli string
`P_A ⊗ O_i` — its own share of the target observable tensored with the
inserted cut observable. **[MENOS]** This is exactly the shape
`common::restrict_observable` already produces, which is why the existing
Phase 5 observable machinery is the natural substrate.

**K cuts.** The decomposition is applied once per cut and the sums nest:

```
<P>  =  sum_{i_1=1}^{8} ... sum_{i_K=1}^{8}  ( prod_{k=1}^{K} c_{i_k} )
        · prod_{f ∈ fragments}  < P_f ⊗ (inserted observables on f) >_{f, prepared states on f}
                                                                          (2.3)
```

giving `8^K` signed terms. **[SOURCE]** Peng Theorem 1 gives total running
time `O(2^{4K}(n+m)/eps^2)` for the *sampled* setting.

**Cross-check, shown rather than asserted.** The 1-norm of (2.1) is
`sum_i |c_i| = 8 × 1/2 = 4`. **[SOURCE]** arXiv:2302.03366 Proposition 4.1
independently proves `gamma_LO = 4^n` for `n` parallel wire cuts, optimal. And
`gamma^{2K} = 16^K = 2^{4K}` reproduces Peng's Theorem 1 exponent. Three
numbers from two independent papers agree. This check is recorded because
Phase 8 shipped a factor-of-two error in a cost constant that no test
compared against anything.

### 2.2 Gate cutting, derived (for completeness; not proposed for Phase 9)

**[SOURCE]** arXiv:2312.11638 defines the overhead of simulating an operation
`E` with a set `S` of implementable operations:

```
gamma_S(E)  :=  min { sum_i |a_i|  :  E = sum_i a_i F_i,  F_i ∈ S,  a_i ∈ R }
                                                                          (2.4)
```

A two-qubit unitary is decomposed into a quasiprobability sum of products of
local channels, `U(·)U† = sum_j a_j (E_j ⊗ F_j)(·)`, with `sum_j |a_j| =
gamma(U)`. **[SOURCE]** For a two-qubit `U` with KAK parameters
`(u_0,u_1,u_2,u_3)`:

```
gamma(U)  =  1 + 2 · sum_{i != j} |u_i| |u_j|                             (2.5)
```

giving `gamma(CNOT) = 3` and `gamma(SWAP) = 7`. **[SOURCE]** Mitarai & Fujii
give an explicit CNOT decomposition using six single-qubit operations
(projective Pauli measurements and `pi/2` rotations), with sampling cost
`O(9^k/eps^2)` for `k` cuts — consistent with `gamma^2 = 9`.

### 2.3 A genuine asymmetry between the two methods **[SOURCE]**

Classical communication between fragments **helps wire cutting** but
**provably does not help two-qubit gate cutting**:

- arXiv:2302.03366 Prop. 4.2: `gamma_LOCC = 2^{n+1} − 1` for `n` wire cuts,
  proven optimal and strictly submultiplicative. For `n = 1` this is 3,
  against 4 without classical communication.
- arXiv:2312.11638: `gamma_LO(U) = gamma_LOCC(U)` for two-qubit gates —
  classical communication gives no advantage, in either the single or the
  parallel setting.

The sources do not disagree; the asymmetry is real, and it matters if the
sampled variant is ever built. It is irrelevant to Phase 9 as proposed,
because exact enumeration pays the term count, not `gamma`.

### 2.4 What is NOT derived here **[OPEN]**

- Reconstruction of a **probability distribution** (counts) rather than an
  expectation. The quasiprobability weights are signed, so the reconstructed
  object is a quasi-distribution, not a distribution. No estimator for this
  is derived here and none should be invented; see §7.
- Cuts that **separate a qubit from itself more than once** (a qubit crossing
  back and forth between fragments) — the identity composes, but the
  bookkeeping of which fragment holds which time interval is a `Region`
  design question, §6.

---

## 3. Exact, sampled, and approximate cutting

**[MENOS]** Definitions for MENOS, so the words are unambiguous in code and
in `explain()`:

- **Exact cutting.** Every branch of (2.3) is evaluated and summed with its
  coefficient. The result equals the uncut expectation up to floating-point
  rounding. `exact = true`. No estimator, no variance, no error bound. Cost
  is the branch count.
- **Sampled cutting.** Branches are drawn with probability `|c_i|/gamma` and
  combined by a sign-corrected estimator. Unbiased, with variance set by
  `gamma`; `exact = false`, and a reported error estimate is mandatory.
- **Approximate cutting.** Any scheme that drops branches or truncates the
  decomposition. Not proposed, and not to be introduced without its own
  derivation.

### 3.1 Recommendation: exact for Phase 9 — **PROPOSED (P2), not approved**

Three reasons:

1. **It is what a classical simulator naturally does.** **[SOURCE]**
   arXiv:2603.01443 (circuit cutting in classical state-vector simulation)
   describes reconstruction as deterministic and exact — "all branches are
   summed" — with no sampling step.
2. **`CLAUDE.md` requires it.** Determinism is a domain invariant, and
   "never silently approximate" means an approximate mode would need a
   derived, reported error bound. Prime directive 6 forbids inventing one.
3. **It removes the entire variance analysis from the critical path.** The
   sampled estimator's mathematics is recorded in §3.2 so the option stays
   open, but Phase 9 does not depend on it.

### 3.2 The sampled estimator, recorded but not proposed

**[SOURCE]** arXiv:2312.11638: to reach additive error `eps` with confidence
`1 − delta`, the number of shots is `C · (sum_i |a_i|)^2 · eps^{-2} ·
ln(1/(2 delta))`, i.e. `O(gamma^{2K}/eps^2)`, matching Peng's `O(2^{4K}/eps^2)`
for wire cuts and Mitarai–Fujii's `O(9^k/eps^2)` for CNOT cuts.

**[OPEN]** MENOS has no sampling noise on a fragment expectation — a
simulated fragment returns the exact value. So a "sampled cut" in MENOS would
sample only the *branch index*, not the fragment outcome, and the variance
expression above (derived for hardware shot noise) does not transfer
unmodified. Deriving the correct variance for branch-sampling-with-exact-
fragments is left open and must not be assumed equal to the hardware result.

---

## 4. Cost model

**[MENOS]** All quantities in existing work units (`docs/adr/0006`). No
seconds, no hardware constants.

### 4.1 Distinct fragment evaluations, not `8^K` of them

A naive reading of (2.3) suggests `8^K` full evaluations. It is fewer, because
each fragment's value depends only on its *own* cut assignments:

- On the **measurement side** a cut contributes one of 4 distinct observables
  (`I, X, Y, Z` — rows 1/2, 3/4, 5/6, 7/8 of the table share an observable).
- On the **preparation side** a cut contributes one of 6 distinct states
  (`|0>, |1>, |+>, |−>, |+i>, |−i>`).

So for a fragment `f` with `m_f` cut wires it measures and `p_f` cut wires it
prepares, the number of distinct runs is `4^{m_f} · 6^{p_f}`, and the total
evaluation work is

```
W_fragments  =  sum_{f}  4^{m_f} · 6^{p_f} · w_f                          (4.1)
```

where `w_f` is that fragment's own cost from its engine's existing
`estimate()` — dense, stabilizer, Pauli or MPS, unchanged.

**[ASSUMPTION]** (4.1) assumes fragment results are cached and reused across
branches. If they are not, the count degenerates to `8^K` evaluations. This
is an implementation property, and the acceptance criteria in §10 require it
to be measured rather than assumed.

### 4.2 Reconstruction

```
W_reconstruct  =  8^K  signed scalar multiply-adds                        (4.2)
```

per observable term. No `2^q` factor appears — this is the whole reason
Phase 9 is scoped to expectation values (§7).

### 4.3 Memory

```
peak_bytes  =  max_f  peak_bytes(f)                                       (4.3)
```

the maximum over fragments, not the sum, because fragments execute
sequentially and each releases its state before the next — the same model
Phase 3's factorized runtime already uses.

### 4.4 The crossover condition

A cut plan is chosen only when, under the existing rule (direct first,
decomposed only if strictly cheaper, ties to direct):

```
W_fragments + W_reconstruct  <  W_direct                                  (4.4)
```

and it is *feasible* only when (4.3) fits the budget. **[SOURCE]** For
two-way equal partitioning of a `q`-qubit circuit, arXiv:2603.01443 gives the
empirical threshold `c_total < q/2`, with measured crossovers where merging
overhead passes preprocessing at `q ≈ 18` and passes subcircuit simulation at
`q ≈ 22`, and reports that two-way cutting extended the feasible qubit count
by only **4 to 6 qubits** under a fixed time budget. That modest figure is
the single most important practical fact in this document, and is why §10.1
requires a measurement before implementation.

---

## 5. Engine interactions

What crosses a cut is narrow: **a Pauli observable on the measurement side,
and a Pauli eigenstate on the preparation side.** So the question for each
engine is only ever (a) can it be initialized in a product of Pauli
eigenstates, and (b) can it compute the expectation of a Pauli string.

| Engine | (a) prepare Pauli eigenstate | (b) Pauli expectation | Notes |
|---|---|---|---|
| Dense | Yes | Yes | No obstacle. |
| Stabilizer | Yes — all six are stabilizer states | Yes, exactly, from the tableau (`+1`/`−1`/`0`) | Fragment must be entirely Clifford, per the existing eligibility rule. |
| Pauli propagation | **[MENOS]** by prepending a Clifford (`H`, `S·H`, `X`) to the fragment | Yes — this is what it exists for | The inserted cut observable enlarges the propagated observable, which interacts with the term-count bound (below). |
| MPS | Yes — product states have bond dimension 1 | Yes | Truncation interacts with exactness (below). |
| Tensor network | **[MENOS]** requires boundary tensors other than `\|0>` | Yes | A contained extension of `build_closed_timelines`, not a new algorithm. |

### 5.1 Pairs

Because the interface is a Pauli observable and a product state, **every pair
is mathematically valid** for expectation output, and reconstruction via
(2.3) is exact whenever both fragments' values are exact. The pairs differ
only in cost and in eligibility:

- **Dense ↔ Dense** — the baseline. Valid, exact. Each fragment costs
  `16·2^{q_f}` bytes; this is the pair that buys the memory reduction.
- **Dense ↔ Stabilizer** — valid, exact, and the most attractive pair: a
  Clifford fragment costs polynomial time and memory, so the cut effectively
  removes it from the exponential budget.
- **Dense ↔ Pauli** — valid, exact, but the inserted observable `P_A ⊗ O_i`
  is *larger* than `P_A`, so a fragment near Pauli propagation's term-count
  bound can be pushed past it by the cut itself. **[MENOS]** The planner must
  re-check the bound against the *inserted* observable, not the original.
- **Dense ↔ MPS** — valid; exact **only if MPS does not truncate**.
- **Stabilizer ↔ Stabilizer** — valid, exact; but if the whole circuit is
  Clifford, `DirectStabilizer` already handles it and no cut should win.
- **Stabilizer ↔ Pauli**, **Stabilizer ↔ MPS**, **Pauli ↔ MPS** — all valid
  and exact under the same conditions as above. Pauli ↔ MPS is the least
  likely to be selected: both are already cheap on the circuits they suit.

### 5.2 Truncating engines inside a cut — **PROPOSED (P3), not approved**

A cut sums `8^K` **signed** terms. If a fragment's value carries error
`delta`, the reconstructed error is bounded by the sum of `|c| · delta` over
branches, i.e. it can be amplified by up to `gamma^K = 4^K`. No source
consulted here derives a tighter composition for this setting.

**Proposal:** in Phase 9 a cut plan is feasible **only if every fragment
engine is exact** — MPS under `Controlled` truncation is therefore not
eligible inside a cut. Deriving the error composition and permitting
approximate fragments is deferred. Inventing a bound instead would be exactly
what prime directive 6 forbids.

---

## 6. Region model: why `qubit set × time range`

Phase 3's `Region` is a **set of qubits** plus the operations touching them.
That is sufficient for factorization, where the interaction graph's connected
components partition the qubits and every region owns its qubits for the
circuit's whole duration.

A wire cut breaks that assumption in one specific way: it splits **one
qubit's world line in time**. The same qubit belongs to the upstream fragment
before the cut and the downstream fragment after it. A qubit-set region
cannot express "qubit 3, operations 0 through 7" as distinct from "qubit 3,
operations 8 onward", so the two fragments are not representable. Hence the
time range, and hence Phase 9 is where it arrives — it would have had no
consumer earlier, which is why `docs/BUILD_PLAN.md` deferred it.

**[MENOS]** Definitions proposed:

- **Cut location**: a pair `(qubit, operation_index)` — the cut sits on that
  qubit's wire immediately before the given operation index.
- **Region**: `(qubit set, [start_op, end_op))`, with the existing
  `local_to_global` mapping unchanged.
- **Start boundary**: per qubit, either `|0>` (the circuit's own initial
  state) or an inserted `rho_i` from the table in §2.1.
- **End boundary**: per qubit, either "carried to the next region" (no
  boundary) or an inserted observable `O_i` folded into the region's
  observable.
- **Execution ordering**: regions are topologically ordered by time along any
  qubit they share. Fragments with no shared qubit are independent.
- **Recombination semantics**: the weighted signed sum (2.3), over the
  per-region scalar expectations — not the tensor product Phase 3 uses.

**[OPEN]** Whether `Region` gains a time range as two new fields or whether
cut plans use a separate structure is an implementation question deliberately
left to the implementation phase, since the public `Region` is part of the
API surface.

---

## 7. Output semantics

| Output | Phase 9 | Why |
|---|---|---|
| **Expectation value** (single Pauli) | **Supported** | (2.3) reconstructs it exactly from per-fragment scalars. |
| **Weighted Pauli sum** | **Supported** | Linearity: reconstruct each term, then apply coefficients through the existing `combine_expectation`. **[MENOS]** Cost scales with term count, since each term has its own restriction `P_f`; the planner must cost the whole observable, not one term. |
| **Counts** | **Not supported** | The reconstructed object is a quasi-distribution with negative weights; turning it into a sampling distribution needs an estimator this document does not derive (§2.4). |
| **Statevector** | **Not supported** | **[SOURCE]** arXiv:2603.01443: full-state merging costs `O(k^{c_max} · 2^q)` and does not shrink with fragment size, so the cut's memory saving is handed straight back. |

This mirrors Phase 8's scope decision, for the same kind of reason, and is the
owner's decision recorded during this design session.

---

## 8. Planner rules — **PROPOSED (P4), not approved**

Placement and priority are an open owner decision. What follows is this
draft's recommendation, not a settled rule; note that the enumeration order
is also the tie-break order, so placement is not cosmetic.

**[MENOS, proposed]** No existing candidate is reordered. `CutAndRecombine`
is appended to the cumulative enumeration order, after `Factorized`.

Eligibility — all must hold:

1. The request is an **expectation** request (§7).
2. At least one admissible cut set exists for the circuit.
3. Every fragment's assigned engine is **exact** for its fragment (§5.2).
4. Peak fragment memory (4.3) fits the budget.

Selection: it competes on `estimated_work_units` like every other candidate.
Ties go to the earlier candidate, so **ties go to direct**.

On the owner's constraint that a cut must not win merely because direct
execution is unavailable: the existing rule already gives the right
behaviour and needs no special case. Infeasible candidates are discarded, and
the cheapest *feasible* candidate wins. If direct is infeasible on memory and
a cut plan is feasible and correct, the cut winning is the ordinary
feasibility rule operating, not a special dispensation — the same way
`DirectTensorNetwork` wins in Phase 8's acceptance test. What must never
happen is a cut being *exempted* from the cost comparison, and §10 requires a
test in each direction.

---

## 9. Validation plan

Every numerical claim gets an independent oracle. The smallest complete set:

1. **Single cut, analytic.** A circuit whose expectation is known in closed
   form (e.g. a Bell pair with `Z⊗Z`, value 1), cut once. Oracle: the
   analytic value.
2. **Single cut, dense oracle.** Small circuits, one cut, compared against
   the uncut dense expectation. Oracle: `DirectDense` on the same circuit.
3. **Multiple cuts.** `K = 2` and `K = 3`, same dense oracle. Confirms the
   nested sum (2.3) and its sign bookkeeping.
4. **Randomized small circuits.** Fixed seeds, printed on failure, across
   qubit counts and observable supports, against the dense oracle.
5. **Each engine pair from §5.1** that the planner can actually produce,
   each against the dense oracle.
6. **Coefficient/normalization regression.** A test that fails if any `c_i`
   or the `1/2` normalization is altered — the factor that (2.1) shows is
   load-bearing.
7. **Distinct-evaluation count.** Assert the number of fragment evaluations
   matches (4.1) and not `8^K`, so the caching assumption in §4.1 is
   measured, not assumed.
8. **Planner, direct wins.** A circuit where cutting is more expensive:
   assert `CutAndRecombine` is enumerated, feasible, and *loses* on cost.
9. **Planner, cutting wins.** A circuit where it is genuinely cheaper: assert
   it wins, and that the result matches the dense oracle.
10. **Exactness flag.** Assert `exact = true` on every cut plan, and that a
    truncating MPS fragment makes the cut plan ineligible (§5.2).

---

## 10. Acceptance criteria

Falsifiable, each convertible directly into a test.

### 10.1 AC-0, the precondition — pre-registered measurement plan

Must pass BEFORE any Phase 9 implementation. This section is written to be
fixed *before* the experiment runs: the family, the matrix and the pass/fail
rule are all stated here in advance, so the result can falsify the phase
rather than be shaped by it. Phase 8 is the reason for this discipline.

#### 10.1.1 Circuit family, chosen from structure **[OPEN O4 — needs sign-off before running]**

**Proposed family: (K,d)-clustered circuits.**

- Two clusters of `d` qubits each, total `n = 2d`.
- **Dense, non-Clifford interaction inside each cluster** — enough depth and
  connectivity that a cluster is individually hard for every structural
  shortcut MENOS has.
- **Exactly `K` qubit wires crossing between the clusters**, `K` small.
- One `T` gate per cluster guarantees non-Cliffordness.
- Fixed seed; a wide-support observable (`Z` on every qubit) so light-cone
  reduction is a no-op and cannot quietly shrink the problem.

**Why this structure, and why it is not rigging.** This is not a family
invented to flatter cutting: it is the class the method's own defining paper
is written about. **[SOURCE]** Peng, Harrow, Ozols & Wu (arXiv:1904.00102)
define exactly the cluster parameters `K` and `d` and state their result for
`(K,d)`-clustered circuits. Choosing the class a method was designed for, and
fixing it before measuring, is the opposite of selecting a workload after
seeing which one wins.

The structural argument, stated as a prediction to be tested rather than an
assumption:

| Candidate | Why it should fail on this family |
|---|---|
| `DirectDense` | `16·2^n` bytes; at `n = 32` that is 68.7 GB, above a realistic budget |
| `DirectStabilizer` | the clusters are non-Clifford |
| `DirectPauli` | dense non-Clifford interaction drives the term count past its bound |
| `DirectMPS` | the Schmidt bound must be small across **every** cut of the linear order; dense intra-cluster interaction makes interior cuts maximal, so no ordering saves it |
| `DirectTensorNetwork` | dense intra-cluster interaction gives the closed network's line graph high treewidth |
| `Factorized` | `K ≥ 1` means one connected component, so it is never enumerated |
| **Cut** | fragments are `≈ d` qubits (`16·2^d` bytes) and the branch count is `8^K`, both small when `K` is small and `d = n/2` |

**Pre-registered falsifier.** If `DirectMPS` or `DirectTensorNetwork` turns
out feasible *and* cheaper than the cut plan at every point in the matrix,
AC-0 **fails** and Phase 9 does not proceed. That outcome is a real
possibility and is the reason for running the measurement.

#### 10.1.2 Benchmark matrix

| Axis | Values |
|---|---|
| Total qubits `n` (`d = n/2`) | 20, 24, 28, 32 |
| Crossing wires `K` | 1, 2, 3, 4 |
| Intra-cluster depth | 4, 8 |
| Memory budget | **realistic**: 7,752,519,680 bytes (the 50%-of-RAM figure `docs/adr/0010` recorded for this machine); **control**: 64 GB |
| Seed | one fixed value, recorded |

The control budget exists specifically to answer the objection the owner
raised: if a result only holds at the tight budget and disappears at 64 GB,
it was budget-gaming and does not count.

#### 10.1.3 What is measured, kept in three separate columns

The owner's requirement to distinguish these is taken literally; conflating
them is how Phase 8's estimator error survived.

1. **Memory feasibility** — per candidate: `estimated_peak_bytes` from its own
   `estimate()`, and feasible/infeasible against each budget with the reason
   string. For the cut: `max_f peak_bytes(f)` per (4.3).
2. **Estimated cost** — per candidate: `estimated_work_units`. For the cut:
   (4.1) + (4.2), computed by calling the **existing** engines' `estimate()`
   on the fragment circuits. No cutting engine is needed to produce this
   number, which is the whole reason AC-0 can precede implementation.
3. **Actual runtime** — wall-clock. Existing candidates are executed where
   feasible. The cut's runtime is measured by **executing the distinct
   fragment evaluations on existing engines** and summing their measured
   times, plus the measured cost of the reconstruction sum. Fragments are
   ordinary circuits, so this is a real measurement, not a projection.

Also recorded per point: `K`, fragment qubit counts, the distinct-evaluation
count `4^{m_f}·6^{p_f}` per (4.1), and the branch count `8^K`, so the gap
between "distinct evaluations" and "branches" is visible rather than assumed.

#### 10.1.4 Mathematical spot-check, run alongside

Before trusting any cost number, verify the identity itself: on a small
circuit (4–6 qubits, `K = 1`) reconstruct `<P>` via (2.2) using existing
engines for the fragments, and compare against the uncut dense expectation.
Agreement to `1e-12` confirms the eight coefficients, the `1/2`
normalization, and the observable bookkeeping before any of it is built into
an engine. A throwaway analysis program, not production code.

#### 10.1.5 Acceptance criterion, fixed before the experiment

**AC-0 PASSES if and only if there exists at least one matrix point
`(n, K, depth)` at which all five hold:**

1. **Cut is feasible**: `max_f peak_bytes(f)` ≤ the realistic budget.
2. **Cut is cheaper on the cost model**: the cut's estimated work
   (4.1)+(4.2) is strictly less than the estimated work of every existing
   candidate that is feasible at that budget; and if no existing candidate is
   feasible, the cut is feasible while they are not.
3. **The margin is not noise**: the cut's estimated work is at most half that
   of the cheapest feasible existing candidate (a factor-2 margin), or no
   existing candidate is feasible at all.
4. **The estimate tracks reality**: at the largest `n` in the matrix where
   `DirectDense` is actually runnable, the measured cut runtime (10.1.3
   item 3) is lower than the measured direct runtime. This prevents a paper
   win that does not survive a stopwatch.
5. **Not a budget artifact**: conditions 1–3 also hold at the 64 GB control
   budget.

**AC-0 FAILS otherwise**, and on failure Phase 9 does not proceed to
implementation: the design is revised, the family is reconsidered on the
record, or the phase is shelved as Phase 8 was. A failing AC-0 is a valid and
useful outcome, not a problem to engineer around.

### 10.2 Implementation criteria

**AC-1.** Reconstructed expectation values match the dense oracle within
`1e-12` absolute (the existing `common/tolerance.h` figure for complex128 at
these sizes) on the validation set of §9 items 1–5.

**AC-2.** The reconstruction is exact and deterministic: repeated runs and
`dev`/`release` builds produce bit-identical values on a fixed circuit, and
every cut plan reports `exact = true`.

**AC-3.** The number of distinct fragment evaluations equals (4.1) for a
circuit with a known cut structure, demonstrating result reuse rather than
`8^K` re-execution.

**AC-4.** The planner chooses direct execution when the cut's estimated total
exceeds it, with the candidate table showing `CutAndRecombine` enumerated,
feasible, and more expensive.

**AC-5.** The planner chooses `CutAndRecombine` when it is genuinely cheaper,
and the result matches the dense oracle on that same circuit.

**AC-6.** A cut plan containing a fragment whose engine would truncate is
reported infeasible, with a reason naming the truncation (§5.2).

**AC-7.** `explain()` shows, for a cut plan: the cut locations, the fragment
count, the branch count, the distinct-evaluation count, and the estimated
versus actual peak fragment memory.

**AC-8.** Counts and statevector requests never enumerate `CutAndRecombine`
(§7), asserted the same way Phase 8 asserts it for `DirectTensorNetwork`.

---

## 11. References

1. T. Peng, A. W. Harrow, M. Ozols, X. Wu, *Simulating Large Quantum Circuits
   on a Small Quantum Computer*, Phys. Rev. Lett. **125**, 150504 (2020),
   arXiv:1904.00102. — Lemma 1 and Eq. (2) (eight-term wire-cut
   decomposition, `c_i ∈ {±1/2}`); proof Eqs. 12–19 (Pauli-basis identity and
   the measure-and-prepare table); Theorem 1 (`O(2^{4K}(n+m)/eps^2)`).
2. L. Brenner, C. Piveteau, D. Sutter, *Optimal wire cutting with classical
   communication*, arXiv:2302.03366. — Proposition 4.1 (`gamma_LO = 4^n`,
   multiplicative, optimal); Proposition 4.2 (`gamma_LOCC = 2^{n+1} − 1`,
   optimal, strictly submultiplicative).
3. K. Mitarai, K. Fujii, *Constructing a virtual two-qubit gate by sampling
   single-qubit operations*, New J. Phys. **23**, 023021 (2021),
   arXiv:1909.07534. — CNOT via six single-qubit operations; `O(9^k/eps^2)`.
4. *Cutting circuits with multiple two-qubit unitaries*, arXiv:2312.11638. —
   Definition of `gamma_S(E)` (2.4); KAK formula (2.5); `gamma(CNOT) = 3`,
   `gamma(SWAP) = 7`; shot count `C(sum|a_i|)^2 eps^{-2} ln(1/(2delta))`;
   `gamma_LO(U) = gamma_LOCC(U)` for two-qubit gates.
5. *Applicability and Limitations of Quantum Circuit Cutting in Classical
   State-Vector Simulation*, arXiv:2603.01443. — Deterministic exact
   branch summation in classical simulators; threshold `c_total < q/2` for
   two-way partitioning; crossovers at `q ≈ 18` and `q ≈ 22`; 4–6 qubit
   extension; full-state merge cost `O(k^{c_max} · 2^q)`.

**Source agreement.** No two sources consulted here contradict each other.
Reference 2's `gamma_LO = 4` and reference 1's eight `±1/2` coefficients agree
exactly (§2.1), and both agree with reference 1's Theorem 1 exponent. The
wire-cutting versus gate-cutting asymmetry in classical communication (§2.3)
is a real difference between two settings, not a disagreement.

---

## 12. Decision register

Three buckets, kept strictly apart. Nothing moves from **PROPOSED** to
**APPROVED** except by the owner saying so.

### 12.1 APPROVED by the owner

| # | Decision | Where |
|---|---|---|
| A1 | Initial output scope is **expectation values only**; counts and statevector are excluded, with reasons recorded | §7 |
| A2 | Phase 9 requires an **AC-0 precondition measurement** before any implementation | §10.1 |
| A3 | AC-0 is **scoped to one structurally justified named circuit family**, fixed in advance, not a benchmark chosen after seeing results | §10.1 |

### 12.2 PROPOSED — recommendations only, NOT approved

These carry a recommendation and its reasoning, because a design document
that refuses to recommend is not useful. **None of them is settled**, and
none may be treated as settled by any later session.

| # | Question | Recommendation in this draft | Live alternative |
|---|---|---|---|
| P1 | Wire cutting or gate cutting? | Wire cutting, for generality and fit with existing observable machinery (§1.5) | Gate cutting has a strictly lower `gamma` for CNOT (3 versus 4) and fewer branches; better when cross-partition interaction is a few CNOTs |
| P2 | Exact enumeration or sampling? | Exact enumeration (§3.1) | Sampling, if branch counts prove prohibitive; its variance for MENOS is itself unresolved (O1) |
| P3 | Are approximate or truncating fragment engines admissible inside a cut? | No — exact fragments only (§5.2) | Allowing truncating MPS fragments, which needs an error-composition bound across `8^K` signed terms that no consulted source provides |
| P4 | Where does `CutAndRecombine` sit in the planner order, and with what priority? | Appended after `Factorized`, competing purely on cost, no reordering of existing candidates (§8) | Any other placement; note the ordering is also the tie-break order, so placement has real consequences |

### 12.3 OPEN — not resolved, and not recommended either way

- **O1** The correct variance for branch-sampling with *exact* fragments
  (§3.2). MENOS fragments carry no shot noise, so the hardware result does
  not transfer unmodified, and it must not be assumed equal.
- **O2** Whether `Region` gains time-range fields or cut plans use a separate
  structure (§6). `Region` is public API.
- **O3** Whether a qubit may cross between fragments more than once (§2.4).
- **O4** Which named circuit family AC-0 uses. A candidate family and its
  structural justification are proposed in §10.1; selecting it is part of
  AC-0 preparation and needs owner sign-off before the measurement runs.

**[ASSUMPTION]** register:

- Fragment results are cached and reused across branches (§4.1); AC-3 is what
  tests it.
- Pauli propagation can accept a Pauli-eigenstate start by prepending a
  Clifford (§5); cheap and exact, but unverified in code.
- The tensor-network engine's boundary tensors can be generalized beyond
  `|0>` (§5); believed contained, but unverified.

---

## 13. What was measured and what was built

Added after implementation. Sections 1-12 above are the design as it stood
*before* AC-0 ran; this section is the record of what actually happened, kept
separate so the two are never confused.

### 13.1 AC-0 results (measured, pre-registered)

The family, matrix and five-part rule were all fixed in section 10.1 before
the measurement. `benchmarks/cut_ac0_benchmark.cpp` reproduces it.

**AC-0 PASSES.** The strongest point is **n=32, K=1**:

| Candidate | Requirement at the 7.75 GB realistic budget | Verdict |
|---|---|---|
| `DirectDense` | 68.7 GB | infeasible |
| `DirectStabilizer` | non-Clifford circuit | ineligible |
| `DirectPauli` | `T` outside its supported set | ineligible |
| `DirectMPS` | 183 GB | infeasible |
| `DirectTensorNetwork` | elimination search exceeds the budget | infeasible |
| **CutAndRecombine** | **2 MB** | **feasible** |

Condition-by-condition: (1) feasible, (2) cheapest -- nothing else is even
feasible, (3) margin unbounded for the same reason, (4) measured runtime
beats measured direct execution by **66x at n=18, 135x at n=20 and 271x at
n=22**, widening with n, and (5) at the 64 GB control budget, where dense and
the tensor network do become feasible, the cut still wins on estimated work by
**66x** -- so the result is not an artifact of a tight budget.

The crossover is real and is reported rather than hidden: **K=8 loses
everywhere**, because 8^K branches overtake the per-fragment saving. At n=20,
cutting stops winning by K=3. This is exactly the behaviour section 4.4
predicts, and it is why the planner compares costs instead of applying a rule.

The section 10.1.4 identity spot-check passed independently: reconstructing
`<ZZZZ>` on a 4-qubit K=1 circuit through Eq. (2.2) agreed with the uncut dense
expectation to **1.2e-16**.

### 13.2 Final implemented decisions

- **P1 wire cutting, P2 exact enumeration, P3 exact fragments only, P4 after
  `Factorized` competing on cost, A1 expectation only** -- all implemented as
  locked. No sampling exists in the code; `estimate_cut` refuses a non-Exact
  accuracy policy outright rather than reasoning about truncated fragments.
- **`Region` gained no time-range field.** Section 6 argued one was necessary
  because a qubit-set region cannot distinguish "qubit 3, operations 0-7" from
  "qubit 3, operations 8 onward". In implementation that turned out to be an
  argument about how Phase 3 *constructs* regions, not about what the struct
  can express: a fragment's time extent is already implied by which operations
  it owns. The public API is therefore unchanged apart from the new `Strategy`
  value and a `CutSummary` for `explain()`. This is a refinement of the design,
  recorded rather than silently diverged from.
- **Cut plans are recomputed in the runtime** rather than carried on `Plan`,
  so no analysis type leaks into the public API. `find_cut_plan` is
  deterministic, and both sides use the same `analysis::kMaxCuts`.
- **Cut finding lives in `src/analysis/`**, not `src/plan/`, because `cost`
  must be able to see it and `cost` sits below `plan`. The wire-cut basis
  lives in `src/ir/` for the same reason: it is written in terms of `Opcode`,
  and `common` sits below `ir`.

### 13.3 Independent confirmation worth recording

On a 6-qubit tree circuit the cut path returns `0.33961366418324329` where the
tensor-network engine returns `0.33961366418324335` -- agreement to **6e-17**
between two entirely independent execution strategies, neither sharing
reconstruction code with the other. That circuit now routes to
`CutAndRecombine` because a cut is genuinely cheaper there, which is the
planner working as designed rather than a regression.

### 13.4 Known limitations

- **Cut finding is bounded and simple.** It searches single and paired cuts
  over the positions where a qubit's timeline has a gap, capped at 48 candidate
  positions and 600 pair evaluations, and rejects any plan where one fragment
  would hold two segments of the same qubit. It is not an optimal partitioner.
  The planner's cost comparison, not the finder's cleverness, decides whether a
  cut is used -- so a weak finder costs opportunities, never correctness.
- **`kMaxCuts` is 2.** The AC-0 data shows cutting stops paying well before
  that bound on the tested family, so raising it would mostly add planning cost.
- **Expectation values only.** Counts and statevector remain unimplemented for
  the reasons in section 7, unchanged.
- **Every fragment must be exact.** A `Controlled` accuracy policy makes the
  cut candidate infeasible rather than approximate.
- **Circuits containing measurements are refused** by the cut finder, since
  Phase 9 reconstructs expectation values only.
- **Unrelated pre-existing bug, found while testing and deliberately not
  fixed:** an all-identity observable reduces the light cone to zero qubits and
  throws `Tableau: num_qubits must be at least 1`. This reproduces on the
  Phase 8 branch and is independent of cutting.

### 13.5 Open questions still open

O1 (variance for branch-sampling with exact fragments), O3 (a qubit crossing
between fragments more than once) and gate cutting (P1's deferred alternative)
are untouched by this implementation and remain open. O2 is resolved: `Region`
needed no change. O4 is resolved: the (K,d)-clustered family was used.
