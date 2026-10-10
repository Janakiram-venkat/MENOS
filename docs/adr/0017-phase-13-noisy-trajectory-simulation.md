# ADR 0017: Phase 13 — noisy trajectory simulation

## Status

**Accepted. Approved, implemented, and the owner accepted the Phase 13 gate on 2026-10-03.** The owner answered section 9 and approved the implementation plan on 2026-10-02. The implementation is commits `58d89df` to `53de60b` on branch `phase-13-noisy-trajectories`, plus a documentation commit. Section 10 maps every gate criterion to its evidence and records where the implementation departs from this design. The gate decision is recorded at the end of section 10.

As in `docs/adr/0014`–`0016`, three kinds of statement are kept separate, plus a fourth for open questions:

- **[MEASURED]**: a result of a command that was run. This draft has none of its own yet; section 8 lists the measurements implementation must produce.
- **[MENOS]**: a design decision proposed here.
- **[REJECTED]**: a design that was considered and abandoned, with the reason.
- **[OPEN]**: a decision that belongs to the owner (section 9).

---

## 1. Context

Phase 12 simulates noise exactly with a density matrix: 16 · 4ⁿ bytes, so about n ≤ 14 on 16 GiB (`docs/adr/0016` section 4). Phase 13 reaches larger noisy circuits by sampling **trajectories**: each trajectory is one pure-state run in which every channel is replaced by one of its Kraus operators, chosen at random with the correct probability. A trajectory needs 16 · 2ⁿ bytes, the dense statevector's size. The price is that results become statistical: an expectation value has a standard error, and the job has to run many trajectories.

Facts about the existing code that the design depends on (read on 2026-10-02 at `2364bdd`):

1. `engines::dense::apply_matrix` is a generic kernel. Its only precondition is the matrix size and distinct operands (`src/engines/dense/apply_matrix.h`); it does not assume unitarity, so it can apply a Kraus operator.
2. `common::Rng` is `std::mt19937_64` with MENOS's own word-to-double conversion. `common::SplitMix64` derives sub-seeds from the job seed; `runtime.cpp` uses it for Factorized region seeds.
3. `RunOptions` has no precision field. `AccuracyPolicy::Controlled(tolerance)` is the MPS engine's per-step singular-value cutoff and nothing else (`include/menos/accuracy_policy.h`).
4. `MENOSEstimator` receives each pub's `precision` and ignores it: it runs with `shots = 0` and returns `stds = 0` (`python/menos/qiskit/estimator.py`).
5. `Observable` coefficients are complex (`include/menos/observable.h`), and `Result::expectation_value()` is complex.
6. Phase 11's replan restarts the job from the beginning under the new plan. A result depends only on the circuit, the final plan and the seed (`docs/adr/0015`).

## 2. Trajectory algorithm — [MENOS]

`Engine::Trajectory`, `Strategy::DirectTrajectory`, CPU only. It lives in `src/engines/trajectory/` and reuses the dense engine's `StateVector`, `apply_matrix`, `CumulativeDistribution` and expectation pass. It is a separate engine, not a mode of the dense engine, because it has its own cost formula, its own outputs (a standard error) and its own feasibility rules. Phase 11's monitor and Phase 12's refusals apply to it as they do to every engine.

One trajectory, for the (light-cone-reduced) circuit:

1. Reset ψ to |0…0⟩.
2. A gate or unitary: `apply_matrix(U)`, exactly as in the dense engine.
3. A channel {Kᵢ} on k qubits, with Mᵢ = Kᵢ†Kᵢ precomputed once per channel:
   - compute the k-qubit reduced density matrix ρ_q of ψ, in one pass over the 2ⁿ amplitudes;
   - pᵢ = Re Tr(Mᵢ ρ_q). These are the exact branch probabilities ‖Kᵢψ‖²;
   - draw one uniform u and pick the first i whose cumulative sum exceeds u. If rounding leaves u above the total, the last i with pᵢ > 0 is taken;
   - apply Kᵢ with `apply_matrix`, then renormalize ψ by its measured norm (one 2ⁿ pass). The measured norm is used rather than √pᵢ so that rounding cannot accumulate across channels.
4. Outputs for this trajectory:
   - expectation: Xₜ = ⟨ψ|O|ψ⟩, computed exactly on the trajectory state (one 2ⁿ pass per term), not estimated from shots;
   - counts: one shot drawn from |ψ|², then the readout error applied exactly as the density-matrix engine applies it (`docs/adr/0016` section 4).

This is correct for every CPTP channel, not just mixed-unitary ones: averaging |ψₜ⟩⟨ψₜ| over the branch choices reproduces Σᵢ Kᵢ ρ Kᵢ† exactly. A faster path for channels whose branch probabilities do not depend on the state (depolarizing) is **not** proposed: it would be a second code path justified only by a speed-up nobody has measured.

**Monitoring.** `at_safe_boundary()` is called between trajectories (the boundary `docs/BUILD_PLAN.md` names) and between operations inside a trajectory, as the dense engine already does. A long single trajectory therefore cannot hide an overrun.

**Execution order.** Trajectories run sequentially. Because each trajectory has its own RNG stream (section 4), running them in parallel later would give identical results, but threading is not proposed here (no speculative code).

## 3. Precision and the trajectory count — [MENOS]

**Counts.** One shot per trajectory: N = shots. Each shot is then an independent sample from the exact noisy distribution, so the counts have the same distribution as the density-matrix engine's counts. Taking several shots from one trajectory was rejected (section 7) because those shots are correlated.

**Expectation.** The caller gives a target standard error ε > 0 (section 9, Q2 proposes `RunOptions::expectation_precision`). With O = c₀I + Σᵢ cᵢPᵢ and C = Σᵢ |cᵢ| over the non-identity terms, each per-trajectory value satisfies |Xₜ − c₀| ≤ C. So Var(Xₜ) ≤ E|Xₜ − c₀|² ≤ C², whatever the circuit and noise. The count is

  **N_ε = max(2, ⌈C² / ε²⌉)**, computed with checked arithmetic,

which guarantees that the true standard error σ/√N is at most ε before anything runs. The planner and the cost model use this same N. N ≥ 2 so that a sample variance exists. If C = 0 (the observable is a multiple of the identity), N_ε = 2.

**Counts and expectation in one job.** N = max(shots, N_ε). Trajectories 0 … shots−1 each contribute one shot; every trajectory contributes Xₜ.

**Reported standard error.** The value is the sample mean X̄, and the reported standard error is

  s/√N, with s² = Σₜ |Xₜ − X̄|² / (N − 1).

This is the genuine (estimated) standard error of the mean, measured from the run, not the bound ε. It is usually smaller than ε because the bound C² is pessimistic. For a complex X it is the standard error of the complex mean; for the Hermitian observables Qiskit sends, X is real.

The bound is conservative, and that is deliberate. An adaptive count (run until s/√N ≤ ε) would usually stop sooner, but the cost model could not know N before running, and the result would depend on the stopping rule, which biases the estimate. It is rejected (section 7).

## 4. RNG streams — [MENOS]

Trajectory t (counting from 0) uses its own `common::Rng`, seeded with

  seedₜ = the (t + 1)-th output of `common::SplitMix64(base)`,

where `base` is the seed the engine receives: the job seed for a Direct plan. SplitMix64's state advances by a fixed constant, so seedₜ can be computed directly from t without generating the earlier outputs. Inside trajectory t, draws happen in a fixed, documented order:

1. one uniform per channel, in operation order;
2. if t < shots: one word for the shot, then one uniform per readout-errored measurement in `measure_targets` order (the density-matrix engine's existing order).

Consequences:

- A result depends only on (circuit, plan, seed). The same seed gives the same result.
- **After a replan,** the job restarts from scratch under the new plan (fact 6), so a trajectory result reached through a replan is identical to running the trajectory plan directly with that seed. The gate test compares the two.
- Trajectory t's stream does not depend on how many trajectories were run before it, which keeps the door open to parallel execution without changing results.

Factorized region seeds also come from `SplitMix64(job seed)`. They cannot collide with trajectory seeds within one run, because the trajectory engine is not a Factorized region engine in Phase 13 (section 5). If it becomes one later, `base` is the region's own seed.

## 5. Planning, cost and outputs — [MENOS]

**Candidate.** `DirectTrajectory` is enumerated only for a noisy job (the Phase 12 definition: any channel or readout-errored measurement) **whose caller has opted in** (section 9, Q1). A job that has not opted in never enumerates it. Every Phase 12 job and every noiseless job therefore plans, runs and explains exactly as today, which is how Phase 13 gate criterion 5 is met. When it is enumerated:

- a statevector request makes it infeasible, with a reason (there is no single final state);
- an expectation request with no precision makes it infeasible, with a reason (it cannot choose N);
- the density-matrix candidate is still listed. The planner picks the cheaper feasible one by its existing ranking, so the choice is made by cost, not by a rule.

**Cost (work units, checked arithmetic).** Per trajectory: the 2ⁿ reset; each gate at the dense engine's existing per-gate cost; each channel on k qubits at 2^(n+k) for the reduced density matrix, plus one `apply_matrix` pass at the dense cost for k operands, plus 2ⁿ for renormalizing; 2ⁿ per expectation term; and, for a trajectory that produces a shot, 2ⁿ to build its distribution. The total is N times that. Peak bytes are 16 · 2ⁿ for ψ, plus 8 · 2ⁿ for the distribution when counts are requested, plus the largest Kraus set (16 · 4ᵏ per operator). Seconds come from the calibrated CPU work-unit rate, because the engine runs the same kernel. That this rate carries over is a claim, so a correlation benchmark measures it before the gate, as `benchmarks/density_matrix_cost_correlation_benchmark.cpp` did in Phase 12.

**Where the choice flips.** Density matrix ≈ G · 2 · 4ⁿ; trajectories ≈ N · G · 2ⁿ (G gates). Trajectories win roughly when N < 2ⁿ⁺¹, and they always win when 16 · 4ⁿ bytes exceed the memory budget. The controlled test for gate criterion 3 uses both effects: a small n with a tight ε where the density matrix wins, a larger n or looser ε where trajectories win, and a memory budget below 16 · 4ⁿ where only trajectories fit.

**Light cone and factorization.** The trajectory candidate is planned on the light-cone-reduced circuit, which is exact for the reason given in `docs/adr/0016` section 5 (channels are trace preserving). Factorized plans do not use the trajectory engine as a region engine in Phase 13: combining independent region estimates needs its own error-propagation rule for products, and nothing requires it yet. Noisy regions keep using the density-matrix engine. This is a stated deferral.

**CUDA.** CPU only. The CUDA trajectory candidate is infeasible, with a reason, as the CUDA density-matrix candidate is in Phase 12.

**Phase 11 replanning.** An overrun on a density-matrix run records the observed floor and calls `plan_job` again, unchanged. If the caller opted in, the trajectory candidate can now win, so the job continues on it instead of being refused. No new planner path is introduced.

**Result.**

- `Result::exact()` is `false` for any result produced by trajectories, including counts-only results, as `docs/BUILD_PLAN.md` requires.
- New: `Result::expectation_standard_error()`, `std::optional<double>`. It holds the reported standard error for a trajectory expectation; it is `std::nullopt` for an exact result. It is not `0.0`, because "exact" and "standard error zero" are different claims.
- `Plan` records the trajectory count N, and ε when one was given.
- `explain()`, for a trajectory plan only: the trajectory count, how it was derived (shots, or ⌈C²/ε²⌉ with C and ε), the reported standard error, and the statement that the result is statistical and not exact.

**Python and Qiskit.**

- The native `RunOptions` binding and `Result` binding gain the new fields.
- `MENOSEstimator`: a pub with `precision > 0` and a noise model passes the precision as ε and returns the reported standard error in `stds`. The `stds` are no longer hard-coded to zero. A pub with `precision == 0` (the default) runs exactly on the density matrix, with `stds = 0`, as in Phase 12. Note that Qiskit's `StatevectorEstimator` defaults to precision 0, and Aer's `EstimatorV2` interprets precision differently, through shots.
- `MENOSBackend`: counts jobs use trajectories only under the opt-in (Q1).

## 6. Statistical validation (gate criteria 1 and 2) — [MENOS]

Every test uses fixed seeds, so its outcome is deterministic and reproducible. The thresholds below are proposed for owner approval (Q4).

1. **Expectations agree with the density matrix.** For each of K fixtures (noise types: depolarizing, amplitude damping, their compositions, readout; 1–3-qubit channels; n from 2 to 10), zᶠ = (X̄ − exact)/se, where "exact" is the Phase 12 density-matrix value. The test passes if every |zᶠ| ≤ z\*, where z\* = Φ⁻¹(1 − 0.001/(2K)): a Bonferroni-corrected family-wise false-alarm rate of 0.001.
2. **Counts agree with the density matrix.** A chi-square goodness-of-fit test at 0.001 against the density-matrix probabilities with readout applied, merging bins with expected count below 5. This is the test already used against Aer in Phase 12.
3. **The standard errors are genuine.** On two fixtures, 400 independent seeds each: (a) the fraction of runs with |z| ≤ 1.96 is consistent with 0.95 under a two-sided binomial test at 0.001; (b) the ratio of the empirical standard deviation of X̄ across seeds to the mean reported se lies inside the 0.001 chi-square interval for 399 degrees of freedom. Test (a) catches a biased mean and test (b) catches a wrongly scaled error.
4. **The bound holds.** For every fixture, the reported se is at most ε plus a sampling allowance, and N equals ⌈C²/ε²⌉.

## 7. Rejected alternatives

- **[REJECTED] Several shots per trajectory.** It is cheaper, but shots from one trajectory are correlated, so the counts would not be independent samples of the noisy distribution, and a chi-square test that assumes independence would be wrong.
- **[REJECTED] Adaptive stopping on the observed error.** The cost model cannot know N in advance, and stopping on the running variance biases the estimate.
- **[REJECTED] Reusing `AccuracyPolicy::Controlled(tolerance)` as ε.** That field is defined as the MPS engine's per-step singular-value cutoff (an invariant in `include/menos/accuracy_policy.h`). Giving it a second meaning would let one number silently control two unrelated things.
- **[REJECTED] A trajectory mode inside the dense engine.** It needs a different cost, a different output and different feasibility rules. A mode flag would split every one of those code paths.
- **[REJECTED] Noisy stabilizer and Pauli-propagation channels in Phase 13** (owner decision, Q3). Each is a separate engine extension with its own verification. The dense trajectory engine is the general case and must exist first; "abstractions follow the second implementation".
- **[REJECTED] A fast path for state-independent (mixed-unitary) channels.** It is unmeasured; see section 2.

## 8. Risks and required measurements

1. **The cost bound is pessimistic.** N_ε uses the worst-case variance C², which can be far above the real one. The planner may then prefer the density matrix when trajectories would have been faster in practice, or refuse when a smaller N would have met ε. This is safe (the error target is guaranteed) but can be slow. **Required measurement:** the ratio of the real variance to C² on the fixture set, recorded in this ADR.
2. **The cost rate may not carry over.** A per-trajectory loop of small kernels on small n is overhead-dominated, so the calibrated rate may be optimistic for small n. **Required measurement:** a trajectory cost-correlation benchmark, like Phase 12's, recorded in this ADR before the gate.
3. **Statistical tests can fail by chance.** With fixed seeds a test either always passes or always fails, so a chance failure would be permanent and would look like a bug. The thresholds in section 6 make that unlikely (0.001 family-wise), and any failure is investigated, not reseeded away.
4. **Expectation with tiny ε.** N grows as 1/ε². Checked arithmetic turns overflow into an infeasible candidate with a reason, never into a wrap-around.
5. **Phase 12 regression.** The opt-in rule (section 5) means no existing job enumerates the new candidate. Every Phase 12 test, explain test and CLI golden file must pass unmodified.

## 9. Owner decisions needed

**Q1. How does a caller opt in to trajectories?** This decides whether any existing noisy job's plan can change.

- *(Recommended)* **An explicit request.** For expectation: setting `expectation_precision` (Q2) is the opt-in. For counts: a new `RunOptions::noise_method` with values `Exact` (the default, density matrix only) and `Auto` (the planner may pick trajectories by cost). In Qiskit, a backend run option `noise_method="auto"`. Default behavior is exactly Phase 12's.
- **Auto by default.** The planner always considers trajectories for noisy jobs. This reaches larger circuits with no extra options, but some Phase 12 noisy jobs would silently change from exact to statistical, which breaks gate criterion 5 and the "never silently approximate" directive.

**Q2. The name and meaning of the precision input.** The proposal is `RunOptions::expectation_precision`, `std::optional<double>`: the target **absolute standard error** of the expectation value, the meaning `EstimatorV2.precision` has in Qiskit. It is also the opt-in for expectation jobs.

**Q3. Noisy stabilizer and Pauli propagation.** `docs/BUILD_PLAN.md` leaves their inclusion to this ADR. The recommendation is **to defer both to a later, separately approved phase.** Noisy stabilizer simulation covers only Pauli noise on Clifford circuits, so it cannot run amplitude damping, one of the three verified types.

**Q4. Statistical thresholds.** Approve the family-wise rate of 0.001, the 400-seed coverage test and the chi-square level in section 6, or set others.

**Q5. Verification environment.** Phase 13 adds no CUDA code. If the Windows driver is still 566.14, criterion 5's `verify.sh` has the same CUDA limitation as Phase 12 criterion 10. Should Phase 13 be accepted on the same basis, or does the owner intend to update the driver (R580+) first?

### Owner answers (2026-10-02)

- **Q1: explicit request** (the recommended option). `expectation_precision` is the opt-in for expectation jobs, and `RunOptions::noise_method` (`Exact` by default, or `Auto`) is the opt-in for counts. A job that does not opt in plans exactly as in Phase 12.
- **Q2: accepted as proposed.** `RunOptions::expectation_precision`, `std::optional<double>`, the target absolute standard error, with N_ε = max(2, ⌈C²/ε²⌉).
- **Q3: defer both.** Phase 13 is dense-statevector trajectories only. Noisy stabilizer and Pauli-propagation channels need a later, separately approved phase.
- **Q4: accepted as proposed.** Bonferroni family-wise rate 0.001, chi-square at 0.001, and the 400-seed coverage and scale tests in section 6.
- **Q5: overtaken by events, not decided by the owner.** On 2026-10-02 the `cuda12-dev` and `cuda12-release` presets were added (`docs/adr/0014` section 17). They build against CUDA 12.4, which driver 566.14 runs, so the GPU tests execute on this machine without a driver update. Phase 13 is verified with those stages in `scripts/verify.sh`.

## 10. Acceptance evidence (2026-10-02) — [MEASURED]

Environment: WSL Ubuntu 26.04, Intel Core Ultra 7 155H, GCC 15.2 (and GCC 13.4 for the CUDA 12 builds), RTX 4060 Laptop GPU with driver 566.14. Additional owner decisions taken during planning: `MENOSEstimator(seed=None)` draws a fresh seed per `run()` and records it in each `PubResult.metadata["seed"]`; precision opts in only the expectation, so a job that also requests counts needs `NoiseMethod::Auto`.

### Gate criteria

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 1 | Trajectories agree with Phase 12 density-matrix results within their reported error, by a documented statistical test on a fixture set | **Pass** | Expectations: `TrajectoryRunTest.ExpectationsAgreeWithTheDensityMatrixWithinTheirStandardError`, 8 fixtures (table below), every \|z\| ≤ 1.72 against the Bonferroni z\* = 3.836. Counts: `TrajectoryRunTest.CountsPassAChiSquareTestAgainstTheDensityMatrix`, χ² = 2.69 against 24.32 (7 dof, 0.001); the exact distribution is the Walsh transform of density-matrix Z-string expectations, pushed through the readout matrix |
| 2 | Reported errors are genuine standard errors; non-exact results are flagged in `Result` and `explain()` | **Pass** | `ReportedStandardErrorsAreGenuineAcrossFourHundredSeeds` (below); `AResultIsFlaggedAndExplainedAsStatistical`; `CountsJobsRunOneShotPerTrajectoryAndAreNotExact`; Python `test_the_estimator_reports_genuine_stds_under_noise_and_precision` (value within 5 se of Aer's exact `EstimatorV2`) |
| 3 | The planner chooses between density matrix and trajectories by cost, shown by a controlled test | **Pass** | `ATightPrecisionOnASmallCircuitLeavesTheDensityMatrixCheaper` (n = 2, ε = 0.01: density matrix); `ALooserPrecisionOnALargerCircuitMakesTrajectoriesCheaper` (n = 12, ε = 0.1: trajectories, and it runs); `AMemoryBudgetBelowTheDensityMatrixLeavesOnlyTrajectories` (n = 8: the cheaper density matrix wins with room, trajectories win below 16 · 4ⁿ bytes) |
| 4 | Deterministic under a fixed seed, including after a replan | **Pass** | `TheSameSeedGivesTheSameResult`; `AReplanFromTheDensityMatrixToTrajectoriesIsDeterministic` (a scripted density-matrix overrun replans through the unchanged `plan_job` to trajectories, and the result equals a direct trajectory run bit for bit); engine tests `TheSameSeedGivesTheSameResult` and `TrajectorySeedIsTheMatchingSplitMix64Output`; Python `test_an_explicit_seed_makes_the_estimator_reproducible` and `test_auto_counts_are_deterministic_under_a_seed`. The criterion 1 z values are identical in the sanitizer and release builds |
| 5 | Phase 12 behavior unchanged; `scripts/verify.sh` passes | **Pass** | `ANoisyJobWithoutAnOptInNeverSeesTheTrajectoryCandidate`, `ANoiselessJobNeverSeesTheTrajectoryCandidateEvenWhenOptedIn`, `PrecisionWithCountsButWithoutAutoStaysOnTheDensityMatrix`. Every pre-existing test, explain test and CLI golden file passes **unmodified** (C++ 551 → 578 tests, Python 195 → 207). `scripts/verify.sh` exit 0 (below) |
| 6 | No Phase 14 features | **Pass** | No mitigation, batching or multi-circuit execution was added |

### Criterion 1: expectation fixtures

ε = C/40 for every fixture, so N = 1600; seed 2026.

| Fixture | n | Noise | Exact | Trajectory ± se | z |
|---|---|---|---|---|---|
| bell_depolarizing | 2 | 1q depolarizing | 0.800000 | 0.790000 ± 0.0153 | −0.65 |
| amplitude_damping | 2 | amplitude damping | 0.017687 | 0.014687 ± 0.0137 | −0.22 |
| ghz_damped | 3 | amplitude damping × 3 | 0.649519 | 0.657799 ± 0.0103 | +0.81 |
| two_qubit_depolarizing | 3 | 2q depolarizing | 0.385557 | 0.392361 ± 0.00569 | +1.20 |
| three_qubit_channel | 5 | correlated 3q bit flip, 1q depolarizing | 0.467588 | 0.469817 ± 0.00379 | +0.59 |
| composition | 6 | amplitude damping then depolarizing | 0.163531 | 0.167255 ± 0.0027 | +1.38 |
| eight_qubits | 8 | 1q depolarizing × 3 | 0.810000 | 0.833750 ± 0.0138 | +1.72 |
| ten_qubits | 10 | amplitude damping × 3, 2q depolarizing | 1.477045 | 1.469181 ± 0.00761 | −1.03 |

On top of z\* · se, the test allows an absolute `1e-12` for floating-point rounding only. That matters only for an observable whose per-trajectory value barely varies. A first draft of the 5-qubit fixture had zero variance (|+⟩⁵ is invariant under its circuit) and showed the term is needed; the fixture was then changed to an informative one.

### Criterion 2: 400 seeds

ε = 0.05 and C = 1, so N = 400 per run.

| Fixture | \|z\| ≤ 1.96 | Binomial p (two-sided, vs 0.95) | (n−1) · s² / mean(se)² | 0.001 interval, 399 dof |
|---|---|---|---|---|
| bell_depolarizing | 378/400 | 0.645 | 423.3 | [312.5, 498.6] |
| ghz_damped | 380/400 | 1.0 | 391.7 | [312.5, 498.6] |

Every run's se was within ε plus five sampling standard deviations of s, as the bound requires.

### Section 8 measurements

`benchmarks/trajectory_cost_correlation_benchmark.cpp`, release build, calibrated rate 3.08 × 10⁸ work units/s, noisy brickwork with counts:

| n | N | measured / predicted seconds |
|---|---|---|
| 4 | 100, 1000 | 1.02, 1.10 |
| 8 | 100, 1000 | 0.95, 0.94 |
| 12 | 100, 1000 | 1.04, 1.06 |
| 16 | 100, 1000 | 1.25, 1.22 |
| 20 | 100 | 1.51 |

Risk 2 (the cost rate): accurate to about 10% up to n = 12 and optimistic at larger n, in the same direction as the density-matrix engine (`docs/adr/0016`). It is measured, not removed: a time budget near the estimate at n ≥ 16 can be overrun, and Phase 11's monitor then handles it.

Risk 1 (the variance bound): for X = Σ Zᵢ on the same circuits, the measured per-trajectory variance divided by C² = n² was 0.030 (n = 4), 0.014 (n = 8) and 0.0072 (n = 12). N_ε was 33 to 140 times larger than these observables needed. The guarantee holds, but the planner will prefer the density matrix more often than a tighter bound would justify. A tighter, observable-specific bound would need its own ADR.

### Departures from this design

1. **`trajectory_count` lives in `src/cost/`, not in the engine.** The planner must not name a concrete engine (CLAUDE.md's architecture rule), and the count is cost-model arithmetic. The engine receives N in its request.
2. **Peak bytes count every channel's precomputed Kᵢ†Kᵢ products**, not only the largest Kraus set (section 5). They are computed once per run and live for all of it, so their total is what is actually allocated.
3. **The `explain()` label is `trajectory`**, because the report's label column is 11 characters wide.
4. **Exactness comes from an engine-level `statistical` flag** in `EngineExecutionResult`: the runtime reports `!accumulated_error_bound && !statistical`, rather than testing the strategy.

### Not verified

- The docs site build: Node is not installed in WSL, so its link check did not run.
- There is no CUDA trajectory engine (section 5), so there is no CUDA test to run for it.
- Trajectory counts are not compared with Aer through `MENOSBackend`. The backend exposes no memory budget, so trajectories cannot be forced from it on small circuits; the counts criterion is tested in C++ against the density matrix instead.

### Verification run

`UV_PYTHON_PREFERENCE=only-managed scripts/verify.sh` on `847cb44`, 2026-10-03: **`verify.sh: all stages passed`, exit 0.**

| Stage | Result |
|---|---|
| Format, clang-tidy (dev/release, benchmarks, bindings, CUDA C++, CUDA benchmarks) | Pass |
| dev (sanitizers), release | 578/578 each |
| Benchmarks build | Pass |
| pytest (editable install) | 207 passed, 1 deselected |
| Wheel build, clean-venv smoke test | Pass |
| docs build | Skipped in WSL (no Node) |
| cuda-dev, cuda-release (CUDA 13.3) | 611/611 each; GPU tests skip, because driver 566.14 cannot run CUDA 13 |
| cuda12-dev, cuda12-release (CUDA 12.4) | 611/611 each, with the GPU tests executing |

### Gate decision

The owner accepted the Phase 13 gate on 2026-10-03, on the evidence above. Phase 13 is COMPLETE.
