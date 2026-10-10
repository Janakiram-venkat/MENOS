# ADR 0009: MPS truncation error bound and accumulation, and the LAPACK dependency

## Status

Accepted.

## Context

`docs/BUILD_PLAN.md` Phase 6 requires one new ADR for the MPS engine
that (a) states the per-step truncation error bound, cites its source,
states plainly that `tolerance` is a per-step cutoff and not a
pre-guaranteed final-error contract, and (b) records the LAPACK
dependency pin. Both are covered here, in one document, per the spec's
own wording ("ADR 'MPS truncation error bound and accumulation' ...
records the LAPACK dependency pin").

## Part 1: LAPACK dependency

### Decision

System LAPACK/LAPACKE via CMake's `find_package(LAPACK)` plus a manual
`find_path`/`find_library` for LAPACKE (CMake's own `FindLAPACK.cmake`,
checked at `/usr/share/cmake-*/Modules/FindLAPACK.cmake` on this
machine, does not know about LAPACKE at all), not a vendored/FetchContent
build.

### Why

- This development machine has no LAPACK, BLAS or Fortran compiler
  installed by default (checked `pkg-config`, `ldconfig -p`, installed
  apt packages, `gfortran` — none present before this phase). Building
  reference LAPACK from source is a large, Fortran-toolchain-dependent
  undertaking that `CLAUDE.md`'s "minimal dependencies" rule does not
  justify when a system package satisfies the need directly.
- On macOS, `find_package(LAPACK)` auto-detects `Accelerate.framework`
  with no install step. On Linux, the system package is a normal `apt`
  dependency, consistent with this project's existing precedent of
  relying on system-provided compilers/toolchains (`docs/adr/0001`)
  rather than vendoring them.
- `LAPACKE_zgesdd` (complex128 divide-and-conquer SVD) is the routine
  used; it is the standard fast choice for a full SVD over `zgesvd`,
  and thin (`jobz='S'`) mode returns exactly `k = min(m,n)` singular
  values/vectors, which is all every caller (the Schmidt-rank bound
  pass, MPS gate application) needs.

### Pin

Installed and verified in this session (Ubuntu, `apt`):

| Package | Version |
|---|---|
| `liblapack-dev` / `liblapack3` | 3.12.1-7ubuntu1 (reference LAPACK 3.12.1) |
| `liblapacke` / `liblapacke-dev` | 3.12.1-7ubuntu1 |
| `libblas-dev` / `libblas3` | 3.12.1-7ubuntu1 |

This is a system package pin, not a `FetchContent` tag/commit pin —
consistent with how this repository already treats the compiler
toolchain itself (`docs/adr/0001`), and distinct from `GoogleTest`/
`nlohmann_json`, which are vendored by commit hash specifically because
their system-package versions are not independently controlled across
platforms. LAPACK's numerical behavior (which singular values/vectors
come back for a given matrix) is standardized well enough by the
reference LAPACK interface that pinning the exact patch version is not
load-bearing for correctness the way it would be for a header-only or
API-unstable dependency; `tests/common/svd_test.cpp` differentially
checks `svd()`'s actual output (reconstruction, orthonormality) against
hand-computable cases regardless of which LAPACK build is linked.

### `LAPACK_COMPLEX_CUSTOM`

This system's installed `lapacke.h` includes `lapack.h` directly, which
only consults the alternative macro `LAPACK_COMPLEX_CPP` inside an
`#ifdef HAVE_LAPACK_CONFIG_H` block that is never reached in this build
— confirmed by reading the installed `/usr/include/lapack.h`, not
assumed from memory. Its documented alternative, `LAPACK_COMPLEX_CUSTOM`
(define `lapack_complex_float`/`lapack_complex_double` before including
the header), is honored directly by this header's own `#ifndef`
guards. `src/common/svd.cpp` defines both as `std::complex<float>`/
`std::complex<double>` before `#include <lapacke.h>`, keeping the
LAPACKE C API's `_Complex`-adjacent typedefs fully encapsulated in that
one translation unit — `src/common/svd.h` exposes only
`std::complex<double>` to every caller.

## Part 2: MPS truncation error bound

### The per-step cutoff (`Controlled(tolerance)`)

`tolerance` is used directly as the relative singular-value cutoff at
each truncating SVD split (`src/engines/mps/apply_gate.cpp`), together
with an optional `max_bond_dimension` cap. This is standard MPS/TEBD
practice (matching, e.g., Qiskit Aer's own MPS truncation-threshold
convention and general TEBD implementations): singular values below
`sigma_max * tolerance` at that one split are discarded, then the kept
singular values are rescaled so the state stays normalized
(`kept_weight = 1 - discarded_weight`, kept values divided by
`sqrt(kept_weight)`).

**`tolerance` bounds only one step.** It is never used to derive a
global, pre-allocated error budget. Per `docs/BUILD_PLAN.md`: "no such
derivation is attempted, because inventing one would violate the
'never invent mathematics' rule without a reviewed source." What *is*
computed is an a-posteriori bound, built after execution from what was
actually discarded at every step that occurred.

### Single-step distance bound

At one truncating split, let the pre-truncation (exact, normalized)
state at that bond be `|ψ⟩ = Σ_i σ_i |i⟩` in its own singular-value
basis (`Σ_i σ_i² = 1`). Let `w = Σ_{discarded i} σ_i²` be the discarded
weight (exactly `TruncationStats::discarded_weight_per_step`'s entry
for this step). The kept, renormalized state is
`|φ'⟩ = (Σ_{kept i} σ_i |i⟩) / sqrt(1 - w)`.

Since the discarded and kept components are orthogonal,
`⟨ψ|φ'⟩ = ⟨φ|φ'⟩ = (1-w)/sqrt(1-w) = sqrt(1-w)`, giving the *exact*
squared distance:

```
‖|ψ⟩ - |φ'⟩‖² = 2 - 2⟨ψ|φ'⟩ = 2(1 - sqrt(1-w))
```

For `w ∈ [0,1]`, substituting `x = 1-w ∈ [0,1]`: `1 - sqrt(1-w) ≤ w`
is equivalent to `x ≤ sqrt(x)`, i.e. `x² ≤ x`, i.e. `x(x-1) ≤ 0`, which
holds for every `x ∈ [0,1]`. So the exact distance above is bounded by:

```
‖|ψ⟩ - |φ'⟩‖ ≤ sqrt(2w)
```

for every single truncating step, regardless of the cutoff mechanism
(relative-tolerance cutoff or `max_bond_dimension` cap) — a rigorous
inequality, re-derived and checked algebraically in this session, not
an asymptotic ("small `w`") approximation. (Note: the naive alternative
bound `sqrt(w)` does **not** hold in general — `2(1-sqrt(1-w))` exceeds
`w` for `w` above roughly `0.4` — so `sqrt(2w)`, not `sqrt(w)`, is the
correct per-step figure.)

### Accumulation over many steps

Let `|ψ_k⟩` be the state that *exact*, truncation-free evolution would
have produced after `k` gates from the same initial state, and `|φ_k⟩`
the state MENOS's engine actually holds after `k` gates, each followed
by its own truncation (a step with no truncation contributes `w=0` and
the bound below adds nothing). Since applying a gate is a unitary
(distance-preserving) map, and truncation-at-step-`k` acts on
`U_k|φ_{k-1}⟩` with the single-step bound above applying to *any*
normalized input:

```
‖|ψ_k⟩ - |φ_k⟩‖ = ‖U_k|ψ_{k-1}⟩ - T_k(U_k|φ_{k-1}⟩)‖
                ≤ ‖U_k|ψ_{k-1}⟩ - U_k|φ_{k-1}⟩‖ + ‖U_k|φ_{k-1}⟩ - T_k(U_k|φ_{k-1}⟩)‖
                = ‖|ψ_{k-1}⟩ - |φ_{k-1}⟩‖ + sqrt(2 w_k)
```

By induction from `‖|ψ_0⟩-|φ_0⟩‖ = 0`:

```
‖|ψ_N⟩ - |φ_N⟩‖ ≤ Σ_{k=1}^{N} sqrt(2 w_k) = sqrt(2) · Σ_k sqrt(w_k)
```

This is exactly `MPSEngine::execute`'s
`result.accumulated_error_bound = sqrt(2.0) * sum_sqrt_weight`
(`src/engines/mps/mps_engine.cpp`) — a rigorous upper bound on the
Euclidean distance between the exact and the actually-computed state,
built entirely from this run's own discarded weights, after execution.

**This is standard-style reasoning in the MPS/TEBD literature**: U.
Schollwöck, "The density-matrix renormalization group in the age of
matrix product states," Annals of Physics 326 (2011) 96-192
(arXiv:1008.3477), is the field's canonical review and defines the
per-step truncation error identically to `w` above (§2.2/§3: "the
truncation error `ε = 1 - Σ_{a≤D} w_a`", i.e. exactly the discarded
Schmidt weight), and its §7 discusses TEBD/tDMRG time evolution and the
practical limitations truncation-error accumulation places on
achievable simulation times. The specific triangle-inequality induction
and the `sqrt(2w)` single-step bound above were derived and verified
from scratch in this session (algebraically, as shown, and empirically
via `tests/engines/mps/schmidt_rank_bound_consistency_test.cpp`'s
`ManyTruncationStepsMeasuredStateErrorStaysUnderTheReportedBound`, an
8-qubit/6-layer brickwork circuit with `max_bond_dimension=2` that
forces more than 5 truncating steps) — they are not transcribed from a
specific equation in that paper; this document does not claim to have
verified an exact equation-by-equation match against it, only that the
per-step-discarded-weight convention and the general
"accumulates additively across steps" reasoning match the field's own
standard treatment.

### Why this is never a pre-execution guarantee

`Controlled(tolerance)` only bounds each *individual* step's discarded
weight (via the relative cutoff), not their sum. A circuit with many
truncating steps can have `Σ_k sqrt(w_k)` — and hence the reported
bound — arbitrarily larger than any single `tolerance`, even though no
step individually exceeded it. `explain()`'s truncated-run output
states this explicitly ("a-posteriori, from this run's own discarded
weights — not a pre-execution guarantee"), and the bound is computed
strictly after execution, from measured discarded weights, never
estimated ahead of time. This distinction is exactly why
`AccuracyPolicy::Controlled` is documented as changing planner
feasibility, not as a promise about the final error.

### Determinism and SVD gauge freedom

`LAPACKE_zgesdd` returns singular vectors that are only defined up to a
phase (1-qubit case) or a unitary rotation degeneracy (repeated
singular values), so the raw MPS tensors are not bit-identical across
LAPACK implementations, builds, or platforms — MENOS makes no such
promise. `discarded_weight_per_step` (a sum of squared singular
values, gauge-invariant) and every physical output built from it
(sampled counts for a fixed seed, expectation values) are portable and
deterministic; `tests/api/determinism_test.cpp`'s
`MPSCountsAndExpectationAreDeterministicForTheSameSeed` checks exactly
these gauge-invariant outputs, never raw tensor values.

## Consequences

- Every MPS-region result that truncated at least once carries a real,
  execution-derived `accumulated_error_bound`; a region that never
  truncated (whether under `Exact` or an unbound `Controlled` run)
  carries `nullopt` and is reported as exact.
- Future work needing a tighter or differently-shaped bound (e.g. a
  bound that accounts for gate-dependent error amplification rather
  than the worst-case unitary-preserves-distance step) should extend
  this ADR or add a new one — this document is the bound MENOS actually
  ships, not a ceiling on future improvement.
