# ADR 0016: Phase 12 — noise channels and exact density-matrix simulation

## Status

**Approved by the owner on 2026-10-01, implemented, and accepted: the owner closed the Phase 12 gate on 2026-10-02 after criterion 10 was re-verified (section 12).** The final code commit is `96a7cd0`, on branch `phase-12-noise-density-matrix`. Section 10 maps every acceptance criterion to its evidence, and section 11 records the verification run. Criterion 10 is only partly met on this machine: the `cuda-dev` test stage fails on an environmental leak report from WSL's CUDA driver library, and no GPU test could execute because the Windows driver supports only CUDA 12.7. Section 12 records how criterion 10 was then met and the owner's gate decision.

As in `docs/adr/0014` and `0015`, three kinds of statement are kept separate:

- **[MEASURED]**: a result produced by a command run on 2026-10-01 on the owner's Windows machine. It came from a throwaway environment outside the repository (Python 3.14.3, `qiskit==2.5.2`, `qiskit-aer==0.17.2`, `uv` virtual environment).
- **[MENOS]**: a design decision made here.
- **[REJECTED]**: a design that was considered and abandoned, with the reason.

---

## 1. Context

MENOS simulates pure states only: every engine evolves a statevector or an equivalent pure-state representation. `docs/reference/limitations.md` lists "Noise is not modelled", and `docs/introduction/overview.md` says MENOS "is **not a noisy simulator**". On 2026-10-01 the owner set a three-phase noise roadmap (`docs/BUILD_PLAN.md`, Phases 12–14) and recorded these Phase 12 decisions:

1. exact density-matrix simulation first, with trajectories deferred to Phase 13;
2. users give noise models as Qiskit Aer `NoiseModel` objects;
3. readout error, depolarizing and amplitude damping are the first verified noise types, and other types are added incrementally.

## 2. Measured facts about Qiskit Aer

These results decide how the Aer-compatible adapter must behave. Each one came from a probe script run against the pinned Qiskit.

1. **[MEASURED] Compatibility.** `qiskit-aer==0.17.2` (released 2025-09-17) installs from a `cp314` wheel next to `qiskit==2.5.2` (released 2026-08-13) and runs. Aer is eleven months older than the pinned Qiskit, so compatibility is a tested fact, not an assumption, and later upgrades to either package need the same check.
2. **[MEASURED] Converting errors to Kraus operators is lossless.** For depolarizing (1- and 2-qubit), amplitude-damping, composed and thermal-relaxation errors, `Kraus(error)` gives a CPTP channel whose superoperator matches the source to a maximum absolute difference of 8.88e-16.
3. **[MEASURED] `qiskit_aer.utils.insert_noise(circuit, noise_model)` is public and resolves Aer's override rules.** With a depolarizing error on `h` for all qubits and an amplitude-damping error on `h` for qubit 1 only, qubit 1 received only the local error (Aer printed its override warning), as `AerSimulator` does. The function inserts one `QuantumChannelInstruction` after each noisy operation, and the public `qiskit.quantum_info.Kraus(instruction)` converts it exactly.
4. **[MEASURED] `insert_noise` does not match `AerSimulator` for errors attached to `measure`.** With a certain bit flip (`pauli_error([("X", 1.0)])`) on `measure`, `AerSimulator(noise_model=...)` returns `{'1': 100}`: the error is applied **before** the measurement. `insert_noise` puts the channel **after** the measurement, and simulating that circuit returns `{'0': 100}`.
5. **[MEASURED] Readout errors are not inserted by `insert_noise`.** They are available through the public `NoiseModel.to_dict()`, as entries of type `roerror` with their `probabilities` and, for per-qubit errors, `gate_qubits`. (`NoiseModel.from_dict` is deprecated. `to_dict` is not, and it is the only public read path.)
6. **[MEASURED] Idle noise survives.** An error attached to `id` is inserted as a separate channel instruction after the `id`. MENOS's converter drops `id`, `barrier` and `delay`, which is harmless because the noise is a separate instruction and is kept.

## 3. Representation in KIR

### 3.1 Channel operation — [MENOS]

`Operation` gains a fourth alternative:

```cpp
// A completely positive, trace-preserving channel given by its Kraus
// operators: rho -> sum_i K_i rho K_i^dagger. Each K_i is a 2^k x 2^k
// row-major matrix, k == qubits.size(), with the same operand-order rule
// as UnitaryOp (bit j of the row/column index is operand qubits[j]).
struct ChannelOp {
    std::vector<QubitId> qubits;
    std::vector<std::vector<std::complex<double>>> kraus;
};
using Operation = std::variant<GateOp, UnitaryOp, MeasureOp, ChannelOp>;
```

It is added to the same variant as `UnitaryOp`, not built as a separate mechanism, following the rule already stated in `include/menos/circuit.h` ("Unitary and Measure are members of this same enum rather than a separate mechanism"). `Circuit::append_channel(qubits, kraus)` appends one. Validation (`ir::validate`) requires 1 ≤ k ≤ `kMaxUnitaryQubits`, at least one Kraus operator, correct dimensions, finite entries, distinct in-range operands, and trace preservation: every entry of Σ K†K must lie within the tolerance of the identity's corresponding entry, the same entrywise form `is_unitary` already uses for `UnitaryOp` (section 9, decision 2). A channel is subject to the terminal-measurement rule exactly as a gate is.

### 3.2 Readout error — [MENOS]

Readout error is classical: it changes the recorded bit, not the quantum state. It is attached to the measurement:

```cpp
// P(recorded | true) for one measured qubit, row-major:
// {P(0|0), P(1|0), P(0|1), P(1|1)}. Each row sums to 1.
struct ReadoutError { std::array<double, 4> assignment; };
struct MeasureOp { QubitId qubit; ClbitId clbit; std::optional<ReadoutError> readout_error = std::nullopt; };
```

The new member has a default, so existing `MeasureOp{qubit, clbit}` construction still compiles. Validation requires finite entries in [0, 1] and rows summing to 1 within tolerance. Readout error affects **sampled counts only**, never expectation values, matching Aer, where readout error applies only to measurement outcomes. Correlated multi-qubit readout errors are not representable in Phase 12: the adapter raises `UnsupportedFeatureError` for them.

### 3.3 No C++ noise-model type — [MENOS]

The C++ core receives explicit channels and readout errors only. Lowering an Aer `NoiseModel` happens in the Python adapter (section 6), because the owner chose Aer compatibility and only Python can read Aer objects. A C++ `NoiseModel` would have no caller: the CLI's OpenQASM 2 has no noise syntax, and the native API can already append channels. Adding one would be speculative code.

## 4. Density-matrix engine — [MENOS]

`Engine::DensityMatrix`, `Strategy::DirectDensityMatrix`, CPU only.

**State.** ρ is stored as a 2n-qubit vector with index `r | (c << n)` (row r, column c), in the existing 64-byte-aligned `StateVector` sized by `checked_pow2`. The memory budget is therefore checked before allocation by the code that already checks it, at 16 · 4ⁿ bytes. Under the default budget of half of physical memory, 16 GiB of RAM allows n ≤ 14.

**Gates.** UρU† is `apply_matrix(U)` on the row qubits `q` followed by `apply_matrix(conj(U))` on the column qubits `q + n`. It reuses the existing generic kernel unchanged, so no new gate kernel is written.

**Channels.** The superoperator S = Σᵢ Kᵢ ⊗ conj(Kᵢ) is built once per channel and applied in place with `apply_matrix` on the 2k operands `[q₀…q_{k−1}, q₀+n…q_{k−1}+n]`. Its matrix index is `r_bits | (c_bits << k)`, which matches the kernel's rule that bit j is operand j. Applying the superoperator in place avoids the second full-size buffer that summing Kᵢ ρ Kᵢ† term by term would need.

**Outputs.** Counts come from the diagonal Re ρ[x, x], through the existing `CumulativeDistribution` sampling. Expectation values are Tr(ρP), one O(2ⁿ) pass per Pauli term. The engine produces no statevector. It does not return the density matrix either: nothing in Phase 12 consumes one, and the Qiskit primitives in scope do not need it (not building it follows the no-speculative-code rule). Global phase is ignored, since it cancels in ρ.

**Readout error during sampling.** For each shot, the true outcome is drawn first. Then, for each measure target in `measure_targets` order, one uniform value decides whether the recorded bit flips according to its assignment row. The draw order is fixed and documented, so a given seed always produces the same counts.

**Monitoring.** `at_safe_boundary()` is called between operations, like every engine that sweeps operations (`docs/adr/0015`).

**Factorized plans.** The engine implements `RegionCountsSampler` (with readout applied per shot) so that it can serve as a region engine.

Two Factorized paths have to see a readout error that sits on an otherwise idle qubit. Both were found while implementing C3 and are covered by regression tests:

- `cost::estimate_factorized` drops plain measurements from each component's operation list, because they add no work. A measurement with a readout error is routed to its component, so that component's engine sees it. Plain measurements are still dropped, so noiseless estimates are unchanged.
- `region_builder`'s idle test treats a region that holds only plain measurements as idle and samples it as a constant |0⟩ without an engine. A region with a channel or a readout-errored measurement is not idle.

Without these two rules, a readout error on a qubit with no gates would have been dropped without any error.

## 5. Analysis, cost and planning — [MENOS]

**Channels in the analysis passes.** A channel's operands are its `qubits` in every analysis pass: the interaction graph, light-cone reduction, factorization, depth, the cut search and treewidth.

- *Light cone.* A channel is in the cone if it touches the cone. This is exact because every channel is validated as trace preserving: a trace-preserving map on qubits outside the causal past of the requested outputs cannot change their reduced state.
- *Factorization.* A channel spanning two components adds an edge, so components stay disconnected only when no operation (gate or channel) joins them. The joint state is then a product ρ₁ ⊗ ρ₂ ⊗ …, so counts are combined independently and expectation values multiply, the same mixed-product rule used today. Inside a noisy job, a component with no channels and no readout error is pure and may run on any engine, which remains exact.

**Cost (work units, checked arithmetic).** A gate on k qubits costs 2 · 2^(2n+k), the two kernel passes over 4ⁿ entries. A channel on k qubits costs 2^(2n+2k), one pass with 2k operands. Setup costs 4ⁿ, counts cost one 2ⁿ pass over the diagonal, and expectation costs one 2ⁿ pass per term. As for the dense engine, no per-shot term is charged: `EngineEstimateRequest` carries no shot count, and that includes the readout draws. Peak bytes are 16 · 4ⁿ, plus the largest superoperator (16 · 16ᵏ), plus the 8 · 2ⁿ diagonal when counts are requested. Seconds come from the calibrated CPU work-unit rate, because the engine runs the same `apply_matrix` kernel that calibration measures. That the rate carries over was a claim, so `benchmarks/density_matrix_cost_correlation_benchmark.cpp` measures it.

**[MEASURED]** Release build in WSL (GCC 15.2, Intel Core Ultra 7 155H). The circuits are noisy brickworks: two layers of H, RZ and CX, depolarizing on every qubit after each layer, every qubit measured, 1,000 shots. A DirectDensityMatrix plan was forced, and the program ran three times:

| n | work units | measured s (runs 1/2/3) | measured / predicted (runs 1/2/3) |
|---|---|---|---|
| 4 | 30,992 | 0.00011 / 0.00012 / 0.00010 | 1.31 / 0.96 / 1.26 |
| 6 | 757,824 | 0.0024 / 0.0026 / 0.0021 | 1.16 / 0.84 / 1.08 |
| 8 | 16,318,720 | 0.054 / 0.058 / 0.053 | 1.23 / 0.86 / 1.25 |
| 10 | 328,205,312 | 1.30 / 1.38 / 1.34 | 1.47 / 1.03 / 1.56 |
| 11 | 1,447,036,928 | 6.26 / 6.66 / 6.26 | 1.61 / 1.13 / 1.66 |
| 12 | 6,325,014,528 | 28.9 / 30.2 / 29.5 | 1.70 / 1.17 / 1.78 |

Measured times agree across runs to within 5%. The ratio varies mostly because the calibrated rate itself varied between processes (3.71e8, 2.45e8 and 3.83e8 work units per second). That variation is already documented, about 1.5× between processes. The ratio rises with n, up to about 1.7× at n = 12: the calibration circuit is cache-resident, but a 268 MB density matrix is not. This is within the factor of about two that `docs/planning/cost-model.md` already gives for the dense model, and far from the MPS model's 42–1,167×. **[MENOS]** The rate is used unchanged and no correction factor is introduced. The mild optimism at large n is recorded as a limitation; Phase 11 monitoring catches an overrun against a time budget.

**Candidates.** The `DirectDensityMatrix` candidate is enumerated **only for a noisy job**, meaning one that contains any `ChannelOp` or any `MeasureOp` with a readout error. For a noisy job:

- every other Direct candidate is still listed and is infeasible, with a stated reason ("operation i is a noise channel; this engine simulates pure states only"), so `explain()` shows why each one lost;
- Factorized is available: noisy regions can use only the density-matrix engine, and pure regions take their cheapest engine;
- CutAndRecombine is never enumerated: the cut search finds no plan for a circuit that contains a channel or a measurement, so no reason string has to be invented (as built; `NoiseRunTest.AFactorizedNoisyExpectationMultipliesExactRegionValues` asserts it);
- CUDA is infeasible for the density-matrix engine (CPU only in Phase 12).

For a noiseless job nothing changes. The density-matrix engine is not enumerated, not offered to Factorized regions, and does not appear in any concatenated "no engine feasible" reason, so the plan, results, errors and `explain()` output stay byte-identical.

**`explain()`.** For a noisy job only, one job-level line gives the channel count and the readout-error count, and the density-matrix candidate appears in the candidate list.

## 6. Qiskit adapter — [MENOS]

- `qiskit-aer==0.17.2` becomes an optional extra (`menos[noise]`) and a test dependency. It is imported lazily, only when a noise model or an Aer error instruction is present, so a default install does not depend on it.
- `MENOSBackend` gains the run option `noise_model` (default `None`), which is also settable through `set_options`, mirroring Aer's run option. Any value other than a `qiskit_aer.noise.NoiseModel` raises `TypeError`.
- Noise attaches to the instructions of the circuit **as submitted**, the same as Aer's, which does not transpile either. A model written for a particular basis needs the circuit transpiled to that basis first. Lowering then runs in this order:
  1. `insert_noise(circuit, noise_model, transpile=False)` places gate noise, with Aer resolving its own override rules (fact 3).
  2. A channel inserted directly after a `measure` on the same qubit is moved to immediately before that `measure`, to match `AerSimulator` (fact 4).
  3. Readout errors are read from `to_dict()` (fact 5) and attached to the matching `MeasureOp`, with a per-qubit error taking precedence over the all-qubit one, as in Aer. A multi-qubit readout error raises `UnsupportedFeatureError`.
- `QuantumChannelInstruction` and `QuantumError` instructions in a circuit, including errors a user appended directly, convert through `Kraus(...)` to `ChannelOp`.
- Every channel from Aer must pass the verified-type rule in section 9.1.
- `MENOSEstimator` gains a `noise_model` option with the same lowering. Readout errors do not affect its expectation values. **[MEASURED]** Aer's own `EstimatorV2` (density-matrix method) does the same: with a certain readout flip and depolarizing 0.3 on `h`, it returns ⟨X⟩ = 0.7, the exact Tr(ρX). `test_the_estimator_ignores_readout_errors_as_aer_does` keeps this checked.
- Native Python: `Circuit.append_channel(qubits, kraus)` and a `readout_error` argument on `append_measure`.
- The CLI and OpenQASM 2 are unchanged.

## 7. Risks

1. **Noise silently dropped (the main risk).** Almost every site that inspects operations dispatches with an `if (get_if<GateOp>) … if (get_if<UnitaryOp>) … else /* MeasureOp */` chain. About 25 sites were found in `src/`. Adding a variant member compiles cleanly and routes a `ChannelOp` into the `else`. Two concrete cases were found by reading the code:
   - `first_non_clifford_reason` (`src/engines/stabilizer/stabilizer_engine.cpp`) returns "no problem" for anything that is neither a gate nor a unitary, so the stabilizer engine would report a noisy Clifford circuit as feasible and run it **without its noise**.
   - The dense engine's `operand_count` and its apply loop treat the unknown operation as a measurement and skip it.

   Either would return clean results for a noisy job with no error, which directly violates "never silently approximate". **Mitigation:** the first implementation commit, before `ChannelOp` exists, rewrites these chains as exhaustive dispatch (`std::visit` with one handler per alternative, or an explicit final `std::get<MeasureOp>`), with no behavior change and the existing tests as proof. Adding `ChannelOp` then fails to compile at every site until each is handled. In addition, a per-engine regression test asserts that each pure-state engine reports a noisy circuit as infeasible.
2. **Semantic drift from Aer.** The adapter composes two public Aer paths that disagree in one measured case (fact 4). The acceptance gate therefore compares against `AerSimulator` itself, not against `insert_noise`.
3. **Aer lagging Qiskit.** Compatibility is measured, not guaranteed (fact 1). Both packages stay pinned.
4. **Small reach.** Exactness costs 4ⁿ memory. The density-matrix engine is a reference tool for small circuits, not a performance feature, and the documentation will say so. Larger noisy circuits are Phase 13.

## 8. Rejected alternatives

- **[REJECTED] Build the channels by reading Aer's private attributes** (`_default_quantum_errors`, `_local_quantum_errors`). It works in 0.17.2, but it is private API and would re-implement Aer's override rules, which `insert_noise` already applies.
- **[REJECTED] Use `insert_noise` output as-is.** It disagrees with `AerSimulator` for errors attached to `measure` (fact 4).
- **[REJECTED] Apply Kraus operators one at a time, accumulating into a second buffer.** That doubles peak memory, at 4ⁿ, the quantity that most limits this engine. The in-place superoperator costs one extra small matrix.
- **[REJECTED] A C++ `NoiseModel` type in Phase 12.** It would have no caller (section 3.3).
- **[REJECTED] Emulating noise on the statevector engines.** That is trajectory sampling, which is inexact and belongs to Phase 13.

## 9. Owner decisions (2026-10-01)

The four questions this ADR first left open were answered by the owner on 2026-10-01, together with approval of the Phase 12 plan.

1. **`Result.exact` for a density-matrix run: `true`.** It means exact for the requested noise model. `docs/concepts/exactness.md` is restated to match. Trajectory results (Phase 13) will report `false`.
2. **Validation tolerance: `common::kDefaultAbsTol` (1e-12)**, for both trace preservation (each entry of Σ K†K against the identity, the same entrywise form as the unitarity check) and readout-row sums. This is the constant the unitarity check already uses, so no new threshold is introduced. Consequence: a hand-built, rounded Kraus set that Qiskit's `is_cptp` (atol 1e-8) accepts can be rejected by MENOS. Aer-generated channels measured about 1e-15 from exact (fact 2) and pass.
3. **Unverified Aer noise types are rejected** with `UnsupportedFeatureError` until each has its own verification against Aer. The rule is in section 9.1.
4. **Verification environment: WSL Ubuntu, CPU and CUDA.** Measured on 2026-10-01: Ubuntu 26.04 (apt provides GCC 15.2 and CMake 4.2.3, the versions Phase 11 was verified with), 22 cores, 15 GiB of RAM, and an RTX 4060 Laptop GPU with compute capability 8.9. That GPU differs from the RTX 3050 (8.6) used for Phases 10–11. The Windows driver 566.14 supports only CUDA 12.7, so it must be updated to the R580 series or newer before the CUDA stages can run. NVIDIA's WSL repository publishes CUDA up to 13.3.1, not the 13.4 used for Phase 11, so the toolkit version is a recorded environment difference. Code built for the presets' `CMAKE_CUDA_ARCHITECTURES=86` runs on 8.9 devices, so the presets are unchanged. The Windows checkout uses CRLF line endings (`core.autocrlf=true`), so verification runs in a separate clone inside the WSL filesystem, and `scripts/verify.sh` is first run once on the unmodified tree as a baseline.

### 9.1 Verified-type rule for Aer noise models — [MENOS]

The rule is applied in the Python adapter to every channel that comes from an Aer noise model, and to every `QuantumError` or `QuantumChannelInstruction` found in a circuit. It is checked on the channel's Pauli transfer matrix R (`qiskit.quantum_info.PTM`), entry by entry, within 1e-12. A channel is accepted only if it is one of:

- **(a) depolarizing on 1 or 2 qubits:** R is diagonal, R₀₀ = 1, and every other diagonal entry equals one common value f;
- **(b) 1-qubit amplitude damping, excited-state population 0:** R = [[1,0,0,0],[0,a,0,0],[0,0,a,0],[t,0,0,b]], with γ = t ∈ [0, 1], a = √(1−γ) and b = 1−γ;
- **(c) a 1-qubit composition of one depolarizing channel D (parameter f) and one amplitude-damping channel A (parameter γ), in either order.** This is the channel Aer builds when both errors are added to the same instruction and qubits. R has the form in (b) with:
  - A after D: a = f√(1−γ), b = f(1−γ), t = γ;
  - D after A: a = f√(1−γ), b = f(1−γ), t = fγ.

  For each order, f and γ are solved in closed form from b and t, and then every entry is checked.

Anything else raises `UnsupportedFeatureError`, naming the instruction, its qubits and the verified types. This includes phase damping, thermal relaxation, non-uniform Pauli errors, reset errors, generalized amplitude damping, and tensor products of 1-qubit errors on a 2-qubit gate. Each of these is enabled later, as the owner decided, only with its own Aer comparison.

The rule covers **Aer noise models only**. The native `Circuit.append_channel` (C++ and Python) accepts any channel that passes KIR validation, because the density-matrix engine applies every channel through the same superoperator path, and that path is verified generically: random CPTP Kraus sets are compared with Aer running `QuantumError(Kraus(...))`.

## 10. Acceptance evidence

Evidence for each Phase 12 gate criterion (`docs/BUILD_PLAN.md`), on the final code commit `96a7cd0`. C++ test names are `Suite.Test`; Python tests are in `python/tests/test_noise_backend.py`.

| # | Criterion | Result | Evidence |
|---|---|---|---|
| 1 | Channels in KIR, validated | Pass | `tests/ir/channel_validate_test.cpp`, 16 tests: valid depolarizing and amplitude damping, parameter edges, not trace preserving, deviation 2e-11 rejected and 2e-14 accepted at the 1e-12 tolerance, empty, wrong size, NaN, zero and too many operands, out-of-range or repeated operand, a channel after a terminal measurement, and readout rows |
| 2 | Matches an independent reference | Pass | Every non-identity Pauli expectation matches Aer's density-matrix `EstimatorV2` to 1e-12 for mixed noise on 1–3 qubits (`test_expectations_match_aer_density_matrix_on_every_pauli`), both composition orders, and idle noise on `id`. The Pauli expectations of all qubits determine the measured distribution. Counts pass a chi-square test at 0.001 against Aer's exact probabilities with readout applied (`test_counts_with_gate_and_readout_noise_pass_a_chi_square_test`). Measure noise acts before measurement, as in `AerSimulator`. Random CPTP channels through the native API match Aer |
| 3 | Noiseless density-matrix run matches dense | Pass | `DensityMatrixEngineTest.NoiselessCircuitsMatchTheDenseEngineOnEveryPauli`: all 64 three-qubit Paulis to 1e-12, covering every gate kind and an explicit unitary |
| 4 | Planner picks the density-matrix engine; others infeasible with reasons | Pass | `NoiseRunTest.ACountsJobWithAChannelRunsOnTheDensityMatrixEngine`, `NoiseRunTest.ExplainShowsTheNoiseAndWhyEachPureEngineLost`, `EngineNoiseRefusalTest.*` (every CPU engine, both noise kinds). CutAndRecombine is never enumerated (`NoiseRunTest.AFactorizedNoisyExpectationMultipliesExactRegionValues`). The CUDA engine's refusal (`CudaEngineNoiseRefusalTest`) compiles but skips without a usable device; see section 11 |
| 5 | Memory refusal at 4ⁿ | Pass | `NoiseRunTest.TheMemoryBudgetIsEnforcedAtTheDensityMatrixSize` (refused one byte under the estimated peak, runs at it), `DensityMatrixEngineTest.TheBudgetIsCheckedAtSixteenTimesFourToTheN` |
| 6 | Light cone and factorization with channels | Pass | `LightConeTest.AChannelTouchingTheConeIsKeptAndOneOutsideItIsDropped`, `.ATwoQubitChannelPullsItsQubitsIntoTheCone`, `.AMeasurementKeepsItsReadoutError`, `InteractionGraphTest.AChannelConnectsItsOperandsLikeAGate`, `NoiseRunTest.NoiseOutsideTheLightConeIsPlannedAsNoiseless`, `.AFactorizedPlanRunsOnlyTheNoisyRegionOnTheDensityMatrixEngine`, `.AFactorizedNoisyExpectationMultipliesExactRegionValues` (⟨Z₀X₁⟩ = −0.32 to 1e-12) |
| 7 | Noiseless jobs unchanged | Pass | Every pre-Phase-12 test passes **unmodified**, including all `explain()` tests and every CLI golden file. The one exception is `planner_test.cpp`'s replan-termination test, extended from five engines to six. The 158 pre-Phase-12 Python tests pass. A noiseless job never enumerates the density-matrix candidate (section 5) |
| 8 | Determinism | Pass | `DensityMatrixEngineTest.TheSameSeedGivesTheSameCountsWithReadoutErrors`, `NoiseRunTest.TheSameSeedGivesTheSameNoisyCounts`, `test_noisy_counts_are_deterministic_under_a_seed`. Across a replan: `ReplanTest.ANoisyJobWhoseDensityMatrixRunOverrunsIsRefusedDeterministically`, where the only engine able to run the job overruns and the replan path gives the same refusal both times |
| 9 | `Result.exact` semantics | Pass | `NoiseRunTest.AnExpectationJobIsExact`, `.ACountsJobWithAChannelRunsOnTheDensityMatrixEngine` and `.AFactorizedNoisyExpectationMultipliesExactRegionValues` assert `exact()`. `docs/concepts/exactness.md` is restated |
| 10 | CPU and CUDA green; `verify.sh` passes | **Pass** (re-verified 2026-10-02) | First run partial: `test cuda-dev` failed on an environmental LeakSanitizer report from WSL's `libcuda.so`, and the GPU tests skipped under driver 566.14 (section 11). Re-verified on `e546581` with CUDA 12 presets and a path-limited suppression: `verify.sh` exit 0, every CUDA stage 584/584, GPU tests executing (section 12) |
| 11 | No Phase 13/14 features | Pass | Review: no trajectory sampling, no error mitigation, no `SamplerV2`, no batch execution |

## 11. Verification run (2026-10-02)

**[MEASURED]** Environment: WSL Ubuntu 26.04 on the owner's Windows machine; GCC 15.2.0, CMake 4.2.3, clang-format and clang-tidy 21.1.8; uv-managed CPython 3.14.7 (`UV_PYTHON_PREFERENCE=only-managed`, because Ubuntu's system `python3.14` lacks development headers); CUDA toolkit 13.3.1; RTX 4060 Laptop GPU (CC 8.9); Windows driver 566.14, which supports CUDA 12.7. `scripts/verify.sh` itself is unchanged.

`scripts/verify.sh` on `96a7cd0`, in a clean WSL clone:

| Stage | Result |
|---|---|
| Format check, clang-tidy (dev/release targets) | Pass |
| dev build and test (sanitizers) | Pass: **551/551** (496 before Phase 12, plus 55 new); 1 test skips itself in sanitizer builds, as before |
| release build and test | Pass: **551/551** |
| benchmarks build, clang-tidy (benchmarks) | Pass |
| Python: editable install, bindings clang-tidy | Pass |
| pytest (editable install) | Pass: **195 passed**, 1 deselected (158 before Phase 12) |
| wheel build, clean-venv wheel smoke test | Pass: 1 passed |
| docs build | Skipped in WSL (no Node). Run on Windows: `npm run build` passes (94 pages, link check, 723 anchors) |
| CUDA: clang-tidy (CUDA C++), cuda-dev build | Pass |
| **cuda-dev test** | **Fails**: 93 of 584 test processes exit non-zero. Each one's test logic passes. LeakSanitizer then reports a 260-byte leak allocated in `cuDriverGetVersion` in `/usr/lib/wsl/lib/libcuda.so.1`, WSL's driver library, with no MENOS frame. Diagnostics: with `detect_leaks=0` added to the preset's `ASAN_OPTIONS`, all 584 pass; the pre-Phase-12 commit `b77133a` fails `RuntimeTest.ExecutesBellCircuitAndPopulatesCounts` identically. Phase 11 was verified on native Linux, where the driver library is different |
| cuda-release test (run separately after the stop, as `verify.sh` writes it) | Pass: **584/584** |
| CUDA benchmark build and its clang-tidy | Pass (existing warnings only, exit 0) |

**Not verified on this machine:** no GPU test executed. With driver 566.14 (CUDA 12.7), the CUDA 13 runtime finds no usable device, so every GPU test skips itself. Phase 12 adds no CUDA code: the CUDA engine only gained the noise refusal, which compiles and is lint-clean, and its test (`CudaEngineNoiseRefusalTest`) skipped. Running the CUDA tests needs a Windows driver of the R580 series or newer.

## 12. Re-verification of criterion 10 and the gate decision (2026-10-02)

**[MEASURED]** The owner decided to close Phase 12 once criterion 10 was re-verified with GPU tests that execute. Two changes, made outside the Phase 12 code, allowed that on the same WSL machine:

1. **CUDA 12.x support** (`docs/adr/0014` section 17). The `cuda12-dev` and `cuda12-release` presets build against CUDA 12.4, which driver 566.14 runs, so the GPU is usable from these builds.
2. **A LeakSanitizer suppression for the WSL driver shim only** (`scripts/lsan-wsl-libcuda.supp`, `leak:/usr/lib/wsl/lib/libcuda.so`, owner-approved), applied in the `cuda-dev` and `cuda12-dev` test presets. A deliberate non-driver leak is still reported with the suppression in place (`docs/adr/0014` section 17).

No file under `src/`, `include/`, `tests/`, `python/`, `cli/` or `benchmarks/` changed between the Phase 12 code commit `96a7cd0` and `e546581`, so this run verifies the Phase 12 code.

`scripts/verify.sh` on `e546581`:

| Stage | Result |
|---|---|
| Format, clang-tidy, dev, release | Pass: 551/551 each |
| Benchmarks, Python, wheel smoke test | Pass |
| docs build | Skipped in WSL (no Node), as in section 11 |
| cuda-dev / cuda-release (CUDA 13.3) | Pass: 584/584 each. Their GPU tests still skip, because the driver cannot run CUDA 13 |
| cuda12-dev / cuda12-release (CUDA 12.4) | Pass: 584/584 each, with the GPU tests **executing** |
| Overall | **`verify.sh: all stages passed`, exit 0** |

`CudaEngineNoiseRefusalTest.RefusesANoiseChannelAndAReadoutError`, which skipped in section 11, passes in both CUDA 12 builds. One CUDA test, `CudaExecutionMonitorTest.TenAbandonsOfAnOversubscribingTotalNeverExhaustTheDevice`, skips by design: it needs a device too small to hold ten 512 MiB states, and this one has about 7.4 GB free.

**Gate decision:** criterion 10 passes, every other criterion was already met (section 10), and **the owner closed Phase 12 on 2026-10-02.**
