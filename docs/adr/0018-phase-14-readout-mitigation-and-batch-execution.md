# ADR 0018: Phase 14 — readout-error mitigation, batch execution and SamplerV2

## Status

**Accepted. Approved, implemented, and the owner accepted the Phase 14 gate on 2026-10-07.** The owner answered section 9 and approved the implementation plan on 2026-10-07. The implementation is commits `48d75f2` to `75ad45f` on branch `phase-14-mitigation-batch`. Section 10 maps every gate criterion to its evidence and records the departures from this design. The owner started Phase 14 on 2026-10-07 and chose, the same day, from the options in `docs/BUILD_PLAN.md` Phase 14:

- **Mitigation technique: readout-error mitigation** (zero-noise extrapolation is not in this phase).
- **`SamplerV2`: in scope.**
- **Branch:** `phase-14-mitigation-batch`, from `tools-benchmark-dashboard` at `74b81d4`.

Section 9 lists the decisions still open. No code is written until the owner has answered them and approved the implementation plan.

The labels follow `docs/adr/0014`–`0017`: **[MEASURED]**, **[MENOS]** (a proposed design decision), **[REJECTED]** and **[OPEN]** (an owner decision, section 9).

---

## 1. Context

Facts about the existing code that the design depends on (read on 2026-10-07 at `74b81d4`):

1. A readout error is a per-measurement 2×2 assignment matrix on a `MeasureOp` (`include/menos/circuit.h`). It changes recorded counts only, never expectation values (Phase 12, decision 8). Correlated (multi-qubit) readout errors are refused (`python/menos/qiskit/noise.py`).
2. `engines::noise::apply_readout` flips each readout-errored bit independently, so the readout channel on m measured clbits is exactly the tensor product A = A₁ ⊗ … ⊗ A_m of per-clbit matrices.
3. `menos::run` plans one circuit with `plan_job` and runs it. Nothing runs several circuits. `MENOSBackend.max_circuits` is 1 and a longer list raises `ValueError`.
4. `common::SplitMix64` already derives deterministic sub-seeds from a job seed (`src/runtime/runtime.cpp`, Factorized regions).
5. `Result::counts()` is `std::map<BitString, std::uint64_t>`. It cannot hold a quasi-probability.
6. There is no `SamplerV2`. `MENOSEstimator` is the only primitive.

Because of fact 1, readout mitigation acts on **counts (sampled distributions) only**. An expectation value from `MENOSEstimator` contains no readout error, so there is nothing to mitigate; it is unchanged.

## 2. Batch execution — [MENOS]

A new public function in `include/menos/batch.h`:

```cpp
[[nodiscard]] std::vector<Result> run_batch(std::span<const Circuit> circuits, const RunOptions& options);
```

- Each circuit is validated, planned by `plan_job` and run exactly as `menos::run` does: it is a loop over `run`, not a second planner. Each circuit's Phase 11 time budget and replans are its own.
- **Seeds.** Circuit i runs with `seed_i`, the (i+1)-th output of `SplitMix64(options.seed)`. The circuits therefore draw independent random streams, which the uncertainty in section 4 requires: if the calibration and target runs shared a seed, their readout flips would be correlated. As a result, `run_batch({c}, o)[0]` and `run(c, o)` differ. The difference is documented, and the single-circuit paths (`run`, `MENOSBackend.run`) do not go through `run_batch`, so they are unchanged.
- The results are returned in input order. A failure in any circuit throws, and no partial batch is returned.

Production callers: readout mitigation (section 3) and `MENOSSampler` with several pubs (section 5).

## 3. Readout calibration and inversion — [MENOS]

**Calibration circuits.** For a target circuit whose surviving measurements are the pairs (qubit_c, clbit_c), c = 1…m, two circuits on the same qubits and clbits are built: *zero*, which measures each qubit_c into clbit_c from |0⟩, and *one*, which applies X to each qubit_c and then measures. They carry the same readout errors as the target's measurements. How gate noise on the X preparation is treated is **[OPEN] Q1**.

**Estimation.** With K calibration shots each:

- ê0_c = (number of *zero* shots recording 1 on clbit_c) / K, which estimates P(1 | 0).
- ê1_c = (number of *one* shots recording 0 on clbit_c) / K, which estimates P(0 | 1).
- Â_c = [[1 − ê0_c, ê1_c], [ê0_c, 1 − ê1_c]]: columns are the true value, rows the recorded value, as in fact 1.

This tensored (local) model is exact for MENOS's readout errors because of fact 2. It is not an approximation of a correlated model, because MENOS refuses correlated readout errors.

**Refusal.** det Â_c = 1 − ê0_c − ê1_c. If det ≤ 0 for any c, mitigation is refused with `UnsupportedFeature`, naming the clbit. A readout that is no better than a coin flip cannot be inverted meaningfully. No tolerance constant is introduced.

**Inversion.** Let p̂ be the target's empirical distribution over the m measured clbits (N shots). The mitigated quasi-distribution is

  q = M p̂,  M = Â⁻¹ = Â₁⁻¹ ⊗ … ⊗ Â_m⁻¹,

computed exactly over all 2^m outcomes in O(m · 2^m) by applying one 2×2 factor at a time. The result is generally dense and can contain negative entries. It sums to exactly 1 up to rounding, because each Â_c⁻¹ preserves column sums. Clbits the circuit never measures stay 0, as in raw counts.

**Memory.** The dense vector costs 8 · 2^m bytes, plus 8 · 2^m for its error (section 4). It is checked against `memory_budget_bytes` (or the default budget, `docs/adr/0003`), and an over-budget mitigation throws `ResourceExceeded` before any circuit runs.

**[REJECTED] Subspace inversion (M3-style),** which solves only on observed bitstrings. It is an approximation, and "never silently approximate" would require a separate error analysis. It is deferred.

**[REJECTED] Projecting q onto the nearest probability distribution.** That changes the estimator and biases it. The raw quasi-distribution is reported, with its negative entries.

## 4. Reported uncertainty — [MENOS], scope **[OPEN] Q2**

The result is never labelled exact. Each outcome's quasi-probability carries a standard error.

**Target shot noise (exact formula).** q is linear in p̂ and Cov(p̂) = (diag p − p pᵀ)/N, so

  Var(q_i) = [ ((M∘M) p)_i − q_i² ] / N,

where M∘M is the entrywise square. It is itself a tensor product (Â_c⁻¹ ∘ Â_c⁻¹), so this also costs O(m · 2^m). It is reported with p̂ substituted for p.

**Calibration noise (first-order delta method).** ê0_c and ê1_c are independent binomial estimates with variance e(1 − e)/K. Since ∂q/∂e = −M (∂A/∂e) q, and a parameter of clbit c enters only factor c of A = A_{m−1} ⊗ … ⊗ A_0, every other factor of M (∂A/∂e) cancels to the identity: ∂q/∂e = −(A_c⁻¹ ∂A_c/∂e) applied to bit c of q alone. (Corrected 2026-10-07: the first draft kept A_j⁻¹ on the other factors. The unit test against finite differences caught it.) The added variance is

  Σ_c [ (∂q_i/∂e0_c)² Var ê0_c + (∂q_i/∂e1_c)² Var ê1_c ],

at a cost of O(m · 2^m). This term is a first-order approximation, so `explain()` labels it that way.

**Overhead.** γ = ∏_c ‖Â_c⁻¹‖₁ (the maximum absolute column sum) is reported. For any observable with eigenvalues in [−1, 1], an expectation computed from q has standard error at most γ/√N from target shot noise.

## 5. Public API — [MENOS], Sampler output **[OPEN] Q3**

C++ (`include/menos/mitigation.h`):

```cpp
struct ReadoutMitigationOptions { std::uint64_t calibration_shots; };  // K, no default (section 9)
class MitigatedResult {
  // quasi_probabilities(): std::map<BitString, double>, all 2^m outcomes
  // standard_errors():     std::map<BitString, double>, same keys
  // gamma(), calibration (ê0_c, ê1_c per clbit), raw(): the unmitigated Result,
  // calibration_results(): the two calibration Results, exact() == false,
  // explain(): the raw plan's explain() plus a mitigation section
};
[[nodiscard]] MitigatedResult run_readout_mitigated(const Circuit& target,
    const RunOptions& options, const ReadoutMitigationOptions& mitigation);
```

The function builds the calibration circuits, calls `run_batch({target, zero, one})`, and then inverts.

Python:

- `menos._core` bindings for the above.
- **`MENOSSampler(BaseSamplerV2)`** with `default_shots`, `noise_model`, `seed` (None draws a fresh seed and records it, as `MENOSEstimator` does) and `readout_mitigation: bool = False`. Each pub returns Qiskit's standard `DataBin` of `BitArray`s per classical register, built from raw counts, so its contract matches `StatevectorSampler`. Parameterized pubs are refused, as in `MENOSEstimator`. Where the mitigated quasi-distribution goes when `readout_mitigation=True` is **[OPEN] Q3**.
- `MENOSBackend` is unchanged. `max_circuits` stays 1, and list submissions remain a recorded deferral. Changing that error path is not needed for this phase and would alter an existing tested contract.

**Opt-in.** Nothing mitigates unless `run_readout_mitigated` is called or `readout_mitigation=True` is passed. Mitigation requested on a circuit with no readout errors still runs: the calibration yields ê = 0, M = I and q = p̂, and that is reported, not silently skipped. Mitigation of a circuit with no measurements is refused with `InvalidCircuit`.

## 6. Validation (gate) — thresholds **[OPEN] Q4**

Proposed fixture set: 6 circuits (Bell, GHZ-3, GHZ-5, a 4-qubit random Clifford+T circuit, a W state on 3 qubits, and a 5-qubit circuit with uneven per-qubit readout errors). Each runs under (a) readout error only and (b) readout plus depolarizing gate noise. Fixed seeds; N = K = 20 000.

- **Moves toward the reference:** total-variation distance to the noiseless exact distribution, TVD(q, p_ideal) < TVD(p̂, p_ideal), on every fixture.
- **Unbiased under readout-only noise:** for (a), every outcome's z = (q_i − p_ideal,i)/se_i passes a Bonferroni-corrected two-sided test at family-wise rate 0.001, the Phase 13 convention.
- **Genuine errors:** a 400-seed coverage test on one fixture. The fraction of seeds with |z| ≤ 1.96 for a fixed outcome lies within the 0.001 binomial acceptance band around 0.95, the Phase 13 method.

## 7. Rejected alternatives

- **Model-based matrices without calibration circuits** (read A from the `MeasureOp`s). This is exact but uses no batch execution and has no calibration uncertainty, so it does not exercise what the phase is meant to build. It is offered as option (b) of Q1, not silently chosen.
- **Mitigating `MENOSEstimator`.** Readout error never reaches expectations (fact 1).
- **A second planner for batches** (packing circuits, sharing state). This is forbidden by `docs/BUILD_PLAN.md` Phase 14, behavior 1.

## 8. Risks and required measurements

- The dense 2^m mitigated vector limits m. The memory check refuses honestly. Implementation records the largest m that runs in under 1 s.
- With depolarizing noise on the X preparations (Q1 option a), calibration also absorbs gate error. That is realistic, but it biases ê slightly, so criterion "unbiased" is applied to fixture set (a) only.
- Qiskit `SamplerV2` contract details (`BitArray` layout and register order) are checked against `StatevectorSampler` in the installed Qiskit 2.5.2, not assumed.

## 9. Owner decisions needed

**Q1. Calibration circuits under gate noise.** (a, recommended) The calibration circuits run under the same noise model as the target, so X gates get their gate error, as on hardware. (b) Calibration copies only the readout errors, so the preparation is noiseless.

**Q2. Uncertainty scope.** (a, recommended) Target shot noise (exact) plus calibration noise (first-order delta method, labelled). (b) Target shot noise only, with calibration uncertainty stated as not included. Also: `calibration_shots` has no default in C++. In `MENOSSampler` it defaults to the pub's shot count (proposed).

**Q3. Where `MENOSSampler` puts mitigated data.** (a, recommended) `PubResult.metadata["readout_mitigation"]` = `{"quasi_dists": {bitstring: q}, "stds": {bitstring: se}, "gamma": γ, "calibration_shots": K, "seed": …}`, leaving the standard `DataBin` untouched. (b) Extra `DataBin` fields.

**Q4. Gate thresholds.** Approve section 6 (6 fixtures × 2 noise models, N = K = 20 000, TVD strictly reduced on every fixture, Bonferroni 0.001, 400-seed coverage), or set others.

### Owner answers (2026-10-07)

- **Q1: full noise model.** Calibration circuits run under the same noise model as the target. The C++ core has no noise model (Python lowers it to channels), so `run_readout_mitigated` takes the two calibration circuits from its caller. The Python layer builds them as `QuantumCircuit`s and lowers them through `apply_noise_model`, exactly like the target. `readout_calibration_circuits(target)` builds the readout-only pair for C++ callers and tests. The core checks that a supplied pair measures exactly the target's surviving (qubit, clbit) pairs, and refuses otherwise.
- **Q2: target plus calibration noise.** Target shot noise uses the exact formula; calibration noise uses the first-order delta method, labelled as first order in `explain()`. `MENOSSampler`'s `calibration_shots` defaults to the pub's shot count.
- **Q3: `PubResult.metadata["readout_mitigation"]`**, with the standard `DataBin` untouched.
- **Q4: section 6 thresholds approved as proposed.**

## 10. Acceptance evidence (2026-10-07) — [MEASURED]

Environment: WSL Ubuntu 26.04, Intel Core Ultra 7 155H, RTX 4060 Laptop GPU, driver 566.14. Code commit `75ad45f`.

### Gate criteria

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 1 | `run_batch` plans each circuit through `plan_job`; result i equals `run(circuit_i)` under seed_i bit for bit; deterministic | **Pass** | `BatchTest.EachResultEqualsRunUnderItsDerivedSeed` (counts, strategy and `exact` equal to `run` under the derived seed); `BatchTest.TheSameSeedGivesTheSameResults`; Python `test_run_batch_runs_each_circuit_under_its_derived_seed`. `run_batch` is a loop over `run`, so there is no other planning path |
| 2 | Mitigated TVD to the noiseless reference strictly below raw on every fixture (6 × 2) | **Pass** | `ReadoutMitigationRunTest.MitigationMovesEveryFixtureTowardTheNoiselessReference` (table below) |
| 3 | Readout-only: every outcome within its reported error, Bonferroni, family-wise 0.001 | **Pass** | `ReadoutOnlyMitigationAgreesWithTheReferenceWithinItsErrors`: 100 outcomes, max abs(z) = 2.993 against z* = 4.417 |
| 4 | Genuine errors: 400-seed coverage test | **Pass** | `ReportedStandardErrorsAreGenuineAcrossFourHundredSeeds` (bell, readout only): outcome 00 covered 385/400 (p = 0.301), χ² = 393.95; outcome 01 covered 374/400 (p = 0.168), χ² = 423.07; interval [312.54, 498.55] |
| 5 | Never exact; errors, γ and calibration reported in the result and `explain()` | **Pass** | `AMitigatedResultIsNeverExactAndExplainsItself`; the Python `metadata["readout_mitigation"]["exact"] is False` assertion in `test_mitigation_metadata_appears_only_on_opt_in_and_moves_toward_the_ideal` |
| 6 | `MENOSSampler` follows the SamplerV2 contract; noisy counts agree with Aer `SamplerV2` by chi-square at 0.001 | **Pass** | `test_the_data_bin_matches_statevector_sampler` (register names, shapes, `num_bits`, `num_shots`, counts); `test_noisy_counts_agree_with_aer_sampler_by_chi_square` (two-sample homogeneity test, 20 000 shots each, GHZ-3 under readout + depolarizing) |
| 7 | Jobs without mitigation unchanged; every pre-existing test, explain output and CLI golden passes unmodified | **Pass** | No pre-existing test file, golden file or explain expectation was edited; the only edits to existing test files are additions to the two CMake source lists. C++ 578 → 602 tests; every earlier test passes. `MENOSBackend`, `MENOSEstimator`, `run` and the CLI are unchanged. One documentation page, `docs/reference/python-api.md`, had its export list updated, because its verification snippet asserts the exact `menos.__all__` |
| 8 | `scripts/verify.sh` passes; no Phase 15 features | **Pass** | Verification run below. No engine, kernel or calibration code changed |

### Criterion 2: total-variation distance to the noiseless reference

N = K = 20 000, seed 2026. Readout errors are P(1|0) = 0.04 and P(0|1) = 0.08 on every qubit, except uneven5, whose rates are given in the test. Gate noise is 1-qubit depolarizing (p = 0.01) on every operand of every gate, calibration X gates included.

| Fixture | Raw (readout) | Mitigated | Raw (+depolarizing) | Mitigated |
|---|---|---|---|---|
| bell | 0.11440 | 0.00559 | 0.12275 | 0.01147 |
| ghz3 | 0.17035 | 0.01213 | 0.18625 | 0.02232 |
| ghz5 | 0.26120 | 0.01675 | 0.29065 | 0.03665 |
| clifford_t4 | 0.07871 | 0.01359 | 0.09676 | 0.02219 |
| w3 | 0.14920 | 0.01423 | 0.17495 | 0.04014 |
| uneven5 | 0.14663 | 0.01397 | 0.15888 | 0.02559 |

### Section 8 measurement: dense-vector size

`mitigation::mitigate` alone, release build, m measured clbits: m = 20: 0.12 s; m = 22: 0.61 s; m = 24: 2.81 s; m = 26: 11.0 s. **The largest m measured under 1 s is 22.** The Python binding's dict conversion adds more on top of this. Beyond the memory budget, the memory check refuses honestly.

### Departures from this design

1. **Delta-method derivative corrected** (section 4). The first draft kept A_j⁻¹ on the other factors. The finite-difference unit test caught it before any gate test ran.
2. **Memory accounting is 40 · 2^m bytes** (five dense double vectors: distribution, q, two variances, one work vector), not 16 · 2^m. `MitigatedResult` holds dense vectors indexed over the measured clbits, with `outcome(i)`, rather than maps, to keep that bound honest.
3. **Calibration runs use K shots and request counts only.** One `RunOptions` cannot express that, so `run_readout_mitigated` uses an internal `run_batch_with_options` with the same seed rule.
4. **`MENOSSampler` does not call `run_batch`**, because its pubs can have different shot counts. Pub j runs under `derive_seeds(seed, n)[j]`, a binding over the same SplitMix64 rule, so the seed rule has one source.
5. **`BitArray` shots are in ascending outcome order.** MENOS returns counts, not a per-shot memory. This is documented in `docs/build/sampler.md`.
6. **Statistical helpers** were copied into `tests/api/test_statistics.h` rather than moved out of `trajectory_run_test.cpp`, which criterion 7 requires to stay unmodified.
7. **The coverage test also applies the Phase 13 chi-square scale check**, on two outcomes, in addition to the binomial coverage test.

### Not verified

- `verify.sh` skipped the docs site build (node/npm is not installed in WSL). `test_docs_snippets.py` runs the new pages' Python snippets, but their sidebar links were not link-checked.
- The CUDA 13 stages skip GPU tests (driver 566.14). The CUDA 12 stages run them. Phase 14 adds no CUDA code.

### Verification run

`scripts/verify.sh` on `75ad45f`: **exit 0**.

- dev 602/602 and release 602/602 (one 24-qubit wall-time test is skipped by design).
- clang-tidy clean.
- pytest 227 passed, 1 deselected (the wheel smoke test, which then passes in its own stage); wheel smoke 1 passed.
- cuda-dev 635/635, cuda-release 635/635, cuda12-dev 635/635, cuda12-release 635/635.

An earlier run failed at the editable install because PyPI was unreachable from WSL ("operation timed out"). The re-run, with the network restored, passed.

### Gate decision

The owner accepted the Phase 14 gate on 2026-10-07, on the evidence above, including the added criterion 6. Phase 14 is COMPLETE.
