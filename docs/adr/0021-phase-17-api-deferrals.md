# ADR 0021: Phase 17 — API surface: recorded deferrals

## Status

**Accepted; Phase 17 COMPLETE (2026-10-10).** The owner accepted the Phase 17 gate on 2026-10-10 on the evidence in section 11 (section 11.1). The owner started Phase 17 on 2026-10-09, put all four candidates of `docs/BUILD_PLAN.md` Phase 17 in scope, and answered section 9 the same day (answers recorded there). Items 1–3 and step 4a are accepted as designed, with item 1 also exposed on the Qiskit `MENOSBackend` and the CLI (Q2). Item 4b was built for `Factorized` plans only after the step-4a measurement (section 10.1), and criterion 4 was reset by the owner (section 10.3). Branch `phase-17-api-deferrals`, from `main` at `a4ae956`, fast-forwarded into `main` at `0af513e`.

Labels follow `docs/adr/0014`–`0020`: **[MEASURED]**, **[DERIVED]**, **[MENOS]** (a proposed design decision) and **[OPEN]** (an owner decision, section 9).

---

## 1. Context

Four deferrals recorded in earlier phases are in scope:

| Item | Recorded in | What exists today |
|---|---|---|
| 1. Explicit backend selection from Python | `docs/adr/0014`; Phase 16 dashboard work | `RunOptions` has **no** backend field in C++ or Python. Only the planner chooses, and only for the direct dense candidate (`src/plan/planner.cpp`, "Backend selection for the dense candidate"). The dashboard and the Phase 16 benchmarks had to use C++ benchmarks to force CPU against CUDA. |
| 2. `time_budget_seconds` in the Qiskit `BackendV2` adapter | Phase 11 (`docs/adr/0015`), Phase 10 | `RunOptions.time_budget_seconds` is exposed in Python (`src/python/bindings.cpp`). `MENOSBackend.run` sets only `shots`, `seed`, `want_statevector` and `noise_method` (`python/menos/qiskit/backend.py`). |
| 3. A time-budget flag for the CLI | Phase 11 | `cli/cli_impl.cpp` parses `--shots`, `--seed` and `--statevector` only. |
| 4. Heterogeneous per-region CPU/CUDA selection | `docs/adr/0014` section 16.2 | `Region::backend` is per region, and `find_execution_engine()` resolves each region independently. But the planner sets `Backend::Cpu` for every region of a `Factorized` or `CutAndRecombine` plan. The missing piece is the decision, plus any measurement showing it pays. |

Constraints every item keeps:
- `plan_job` is the only planner. The runtime never plans (Phase 11).
- Every default is unchanged. A job that sets none of the new options gets the same plan, results, `explain()` output and CLI golden output as before.
- Nothing is silently approximated or silently overridden. A request that cannot be honoured is refused with a named exception, never quietly replaced.
- CPU and CUDA statevectors are bitwise identical, so counts are identical. Expectation values agree within `1e-12` but not bitwise, because the device reduction order differs (`docs/adr/0014`).

## 2. Item 1: explicit backend selection — [MENOS]

**API.** A new field `RunOptions::backend`, of type `std::optional<Backend>`, using the existing `enum class Backend { Cpu, Cuda }` from `include/menos/plan.h`. Python gets `menos.Backend` (an `nb::enum_`) and `RunOptions.backend`.

- **Unset (the default):** the planner decides, exactly as today.
- **`Cpu`:** no candidate is ever moved to CUDA. Every plan runs on the CPU. This is the same as a build without CUDA.
- **`Cuda`:** a request to run on the GPU. The algorithm is still chosen by the planner, but **only among candidates that can run on CUDA**. Today that is the direct dense candidate; with item 4, also dense regions of `Factorized` and `CutAndRecombine` plans. Every other candidate is marked infeasible with the reason "backend=cuda requested", and `explain()` lists it as such.

**Refusals.** These are proposed, and decided in Q1:
- In a build without CUDA, or with no usable device, `backend = Cuda` throws `UnsupportedFeature`, naming the reason.
- If CUDA is present but no candidate fits the device memory, the host memory budget or the time budget, it throws `ResourceExceeded`, as any infeasible job does today.

**Replanning (Phase 11).** A forced backend stays forced across replans. A CUDA-forced run that overruns its budget is not steered to the CPU; it is refused with `ResourceExceeded` if no CUDA plan meets the budget. The observation still raises the CUDA floor as usual.

**Results.** When both runs use dense simulation, CPU and CUDA statevectors and counts are bitwise identical, and expectation values agree within the documented tolerance (`docs/adr/0014`; `CpuCudaDifferentialTest`). But forcing CUDA can also change the **algorithm**: a job the planner would give to the stabilizer, MPS or another CPU-only engine runs as dense simulation instead. The answer stays exact, but counts are then a different sample of the same distribution for the same seed, because a different engine draws them. *Correction (2026-10-09, found by `CudaBackendRequestDeviceTest` during implementation): the proposal said forcing never changes counts, which holds only when the algorithm is the same.*

**`explain()`.** It records that the backend was requested, not chosen. Proposed line: `backend=cuda (requested)`. Jobs without the option print exactly what they print today.

**Where it is exposed.** Proposed: C++ `RunOptions` (it has to exist there) and the Python `RunOptions`. Whether the Qiskit adapters (`MENOSBackend`, `MENOSEstimator`, `MENOSSampler`) and the CLI also expose it is Q2.

## 3. Item 2: time budget in the Qiskit adapter — [MENOS]

- **`MENOSBackend`:** gains a run option `time_budget_seconds=None` (in `_default_options`), passed to `RunOptions.time_budget_seconds`. `None` keeps today's behaviour exactly. The value must be finite and greater than 0, which the C++ layer already validates.
- **On overrun with no feasible replan:** `menos_run` raises `ResourceExceededError`, and `MENOSBackend.run` lets it propagate, as it does today for a memory-budget refusal. The job is synchronous, so the error reaches the caller at `run()`, not at `result()`.
- **The replan record:** the abandoned attempt (`Result::replans()`) is reported in the Qiskit `Result`'s metadata, under a `menos` key, so nothing is hidden. Proposed fields: the threshold, the observation, and the old and new plan, as in `explain()`.
- **`MENOSEstimator` and `MENOSSampler`:** whether they also take a budget is Q3. A budget applies to each circuit (each PUB), because `run_batch` plans each circuit separately (Phase 14).

## 4. Item 3: CLI time-budget flag — [MENOS]

- **The flag:** `--time-budget SECONDS`, accepted by `menos run` and `menos explain`. It is parsed as a finite double greater than 0; anything else is a usage error that names the flag, as `--shots` is today.
- **What it does in each command:**
  - `explain` plans with the budget, which only affects feasibility, since nothing is executed.
  - `run` also monitors the attempt and replans on overrun, exactly as `RunOptions` does.
- **Output:** `explain()` already prints the budget and any replans when one is set. Without the flag, every CLI golden file is unchanged.

## 5. Item 4: per-region CPU/CUDA selection — [MENOS], measured first

`docs/adr/0014` section 16.2 deferred this for two reasons. First, choosing per region means costing an assignment rather than making one comparison. Second, regions run one after another against one device, which interacts with device memory and transfers in ways nothing had measured. So this item follows the Phase 15 and 16 pattern: **measure first, then decide**.

**Step 4a: measurement [MEASURED, to be done].** A benchmark (benchmark-only code) runs `Factorized` plans with each region forced to CPU and to CUDA, on circuits whose regions span the crossover (for example two or three independent blocks of 12–24 qubits). It records:
- each region's time on each backend, with transfers;
- whether the sum of per-region best choices beats the all-CPU plan, and by how much.

For `CutAndRecombine` it measures one fragment evaluation on each backend, because a cut plan evaluates each fragment many times. The planner's estimate of each region is recorded beside the measured time.

**Step 4b: the decision rule [MENOS, if 4a shows a benefit].** Each region of a `Factorized` plan takes the backend whose calibrated seconds are lower, using the same comparison, tie-break (CPU), device-memory check and observed floors as the dense candidate. Regions run sequentially and each allocates its own state, so per-region choices are independent, and the plan's estimate is the sum. For `CutAndRecombine`, each fragment's choice multiplies its per-evaluation cost (transfers and launches included) by its evaluation count. The candidate's algorithm is still chosen by work units first, so which algorithm wins does not change; only backends do.

**What does not change:**
- An overrun of a `Factorized` or `CutAndRecombine` plan is still refused rather than replanned. Phase 11 does not attribute elapsed time per region, and that deferral stays.
- Results do not change, because the backends are bitwise identical for statevectors and counts. Expectation values can differ within tolerance, as for item 1.
- `explain()` prints each region's backend. Today it always prints `cpu`.

**If 4a shows no material benefit,** the ADR records the measurement and item 4 is closed as "measured, not worth it". That is a legitimate outcome, as the Phase 15 fusion deferral was. Q4 asks the owner to confirm this rule and the threshold for "material".

## 6. Order of work

1. Items 2 and 3 first. They are small, use existing machinery, and need no new planner logic.
2. Item 1.
3. Item 4a (measurement), then the owner's decision, then item 4b. Item 4b also changes how `backend = Cuda` (item 1) behaves for `Factorized` plans.

Each step is one or more small commits with tests, as in earlier phases.

## 7. Out of scope

- `MENOSBackend.max_circuits` stays 1 (Phase 14 deferral).
- Per-operation tensor-network boundaries, the dense sampler's per-shot monitoring, and checkpointing all stay deferred (Phase 11).
- Diagonal and permutation CUDA kernels, and asynchronous launches, stay deferred (Phase 16).
- The interoperability roadmap (Phases 18–25) is out of scope.

## 8. Risks

| Risk | How it is caught |
|---|---|
| A default changes | Every existing test and golden file passes unmodified. New tests assert that `explain()` without the new options is byte-identical to before. |
| `backend = Cuda` silently runs on CPU | A test asserts the executed region's backend and the refusal paths. GPU tests run in the `cuda12` presets. |
| A forced backend is lost on replan | Phase 11 replanning tests extended with a forced backend. |
| The Qiskit budget is set but not honoured | An adapter test with a tiny budget expects `ResourceExceededError`, and one with a generous budget expects normal results. |
| Per-region selection chosen on noisy calibration | 4a measures it; the decision uses the same comparison and floors as today; changed planner decisions are recorded, as in Phases 15 and 16. |
| GPU tests skip silently | The gate requires the `cuda12` builds, where they execute (Phase 16 evidence). |

## 9. Owner decisions needed

- **Q1 — `backend = Cuda` semantics and refusals.**
  - **Semantics, proposed:** only CUDA-capable candidates stay feasible; the others are marked infeasible with a named reason.
  - **Refusals, proposed:** `UnsupportedFeature` if CUDA is not built or no device is present, and `ResourceExceeded` if nothing fits.
  - **The alternative:** treat `Cuda` as a preference that falls back to the CPU. That would be a silent override, so it is not proposed.
- **Q2 — Where item 1 is exposed.**
  - **Proposed:** C++ and Python `RunOptions` only. The Qiskit adapters and the CLI do not get it in this phase.
  - **Alternatives:** also a `backend` run option on `MENOSBackend`, and/or `--backend cpu|cuda` on the CLI.
- **Q3 — Item 2's reach.**
  - **Proposed:** `MENOSBackend` only.
  - **Alternative:** also `MENOSEstimator(time_budget_seconds=...)` and `MENOSSampler(time_budget_seconds=...)`, applied per PUB.
- **Q4 — Item 4's acceptance rule.**
  - **Proposed:** step 4a is measured first. The owner then decides whether 4b is built, and sets its target from the data: for example, a minimum end-to-end speed-up of a mixed-backend `Factorized` plan over the all-CPU plan on the measured family.
  - **If 4a shows no material benefit:** item 4 closes as measured and not adopted.
- **Q5 — The gate.**
  - **Proposed:** each item has regression tests and documentation; every default is unchanged (existing tests and golden files pass unmodified); `scripts/verify.sh` passes with GPU tests executing in the `cuda12` builds; item 4's numeric target is set after 4a (Q4).

### Owner answers (2026-10-09)

1. **Q1:** as proposed. `backend = Cuda` keeps only CUDA-capable candidates feasible. No CUDA build or device raises `UnsupportedFeature`, and nothing fitting raises `ResourceExceeded`. There is never a fallback to the CPU.
2. **Q2:** exposed in C++ and Python `RunOptions`, **and** as a `backend` run option on the Qiskit `MENOSBackend` (`None`, `"cpu"` or `"cuda"`; `None` is the default and means the planner decides), **and** as `--backend cpu|cuda` on `menos run` and `menos explain`. Without the option or flag, behaviour and golden output are unchanged.
3. **Q3:** as proposed: `MENOSBackend` only. `MENOSEstimator` and `MENOSSampler` do not take a budget in this phase.
4. **Q4:** as proposed. Step 4a is measured first. The owner then decides whether 4b is built and sets its target from the data. If there is no material benefit, item 4 closes as measured and not adopted.
5. **Q5:** as proposed. The gate is written in `docs/BUILD_PLAN.md`, Phase 17.

**Owner rulings during implementation (2026-10-09).** Gate criterion 1 is reworded: forcing CPU or CUDA gives bitwise-identical results whenever both runs use dense simulation, and forcing CUDA may change the algorithm (section 2). Under criterion 5, the Phase 11 test `test_the_qiskit_backend_still_exposes_no_time_budget`, which pinned the deferral item 2 lifts, is replaced by tests of the new option and does not count against the criterion.

---

## 10. Step 4a: per-region measurement (2026-10-09) [MEASURED, `docs/benchmarks/phase-17-4a/`]

`menos_cuda_factorized_backend_benchmark` on the Phase 16 gate machine (RTX 4060 Laptop GPU, WSL, `cuda12-release`), on AC throughout, 10 repeats per point after a warm-up, 1000 shots, every qubit measured.

**Run a (`323ec10`) measured nothing usable, and that is itself a finding.** It used blocks of the crossover circuit family. Their nearest-neighbour CX layers are cheap for the MPS engine, so the planner chose `direct_mps` for every multi-block circuit (not `factorized`, not dense). Inside a factorized plan each region also takes its own cheapest engine, which for such blocks is MPS. **A region's backend is a choice only when the region is dense.** The forced-CPU fragment runs of run a were MPS too, so they did not compare dense against dense. The log is kept in `run-a/`.

**Run b (`83818bf`)** uses scrambling blocks: 6 layers of H and T on every qubit, then CX on a random perfect pairing that changes every layer. The planner gives each such block its own **dense** region of a `factorized` plan, and keeps every region on the CPU. Each plan was executed under every CPU (C) / CUDA (G) assignment of its regions through `runtime::Runtime`:

| Blocks (qubits) | All CPU, the planner's own (s) | Best assignment (s) | Best ÷ all CPU |
|---|---|---|---|
| 12 + 22 | 1.732 | GG 1.062 | 1.63× |
| 16 + 20 | 0.481 | GG 0.326 | 1.48× |
| 20 + 22 | 2.284 | GG 1.639 | 1.39× |
| 10 + 16 + 22 | 1.791 | CGG 1.326 | 1.35× |
| 14 + 18 + 22 | 1.844 | GGG 1.406 | 1.31× |

[DERIVED] The gain comes from the largest region (20–22 qubits). Moving a 10–16-qubit region alone changes the total by −6% to +3%, which matches the dense crossover near n = 14–16 (`docs/adr/0020` section 12). The rule "each dense region takes the backend the existing comparison prefers" would give CG for 12 + 22 (1.177 s, 1.47×), and comparable assignments elsewhere.

**Cut fragments** (one dense expectation evaluation of a scrambling block; both runs checked to be dense):

| n | 8 | 10 | 12 | 14 | 16 | 18 |
|---|---|---|---|---|---|---|
| CPU ÷ CUDA | 0.28× | 0.36× | 0.59× | 0.77× | 0.85× | 1.21× |

A cut plan exists to make fragments small, and below 18 qubits a fragment evaluation is slower on CUDA. Per-fragment selection is therefore not worth building.

### 10.1 Owner decision (2026-10-09)

Item 4b is built for **`Factorized` plans only**. Each dense region takes the backend the planner's existing calibrated comparison prefers, with the same tie-break (CPU), device-memory check and observed floors as the direct dense candidate. `CutAndRecombine` plans keep every region on the CPU, and that part of item 4 is closed as measured and not adopted. The target is written in `docs/BUILD_PLAN.md` gate criterion 4. Implementation needs its own plan and owner approval.

### 10.2 Item 4b as implemented (2026-10-09)

`plan_job` decides the Factorized candidate's region backends at the candidate stage, after the direct dense candidate's backend and before the time-budget check, so the budget is judged against the backends that will actually run. The regions are built with `build_factorized_regions`, the same call the winner branch makes. For each non-idle dense region:
- the CUDA engine's own estimate is taken;
- the region moves to CUDA only if that estimate is feasible, fits `cuda_device_available_bytes`, and is strictly faster than the CPU (ties go to the CPU);
- non-dense regions stay on the CPU.

If no region moves, nothing about the candidate or its plan changes. If at least one moves, the candidate's `estimated_seconds` is the sum of each region's seconds on its chosen backend. No observed floor applies: a Factorized candidate carries none (Phase 11). The winner branch applies the same backends to the plan's regions, with the region count checked.

Forced CUDA (item 1) now admits a Factorized plan when every non-idle region is dense and fits the device, with all of them on CUDA. Otherwise the candidate is infeasible with the region named, for example `backend=cuda requested: region 0 runs on mps, which has no CUDA backend`. Forced CPU skips the decision.

The runtime is unchanged: `engine_for(region.engine, region.backend)` already resolved each region. Tests: `FactorizedRegionBackendTest` (seven planner cases on a synthetic profile, CUDA builds) and `CudaBackendRequestDeviceTest.AFactorizedPlanOnTheDeviceGivesIdenticalCounts` (on the GPU, all regions on CUDA against all on CPU, counts bitwise identical).

### 10.3 Item 4b gate runs and the owner's reset (2026-10-09) [MEASURED, `docs/benchmarks/phase-17-4b/`]

There were two gate runs on `e214c27`, both on AC throughout. The figure is the planner's own assignment against all-CPU, same run:

| Blocks | Run a | Run b |
|---|---|---|
| 12 + 22 | CG 1.33× | CG 1.21× |
| 16 + 20 | GG 1.29× | GG 1.25× |
| 20 + 22 | GG 1.07× | GG 0.95× |
| 10 + 16 + 22 | CGG 1.33× | CGG 1.43× |
| 14 + 18 + 22 | CGG 1.07× | CGG 0.98× |

No circuit was more than 10% slower than all-CPU. The all-CPU times matched step 4a (20 + 22: 2.28 s in 4a, then 2.27 s and 2.33 s), while every assignment that used the GPU was 30–50% slower than in 4a (20 + 22 GG: 1.64 s, then 2.13 s and 2.44 s). In run b, even the best assignment for 20 + 22 was all-CPU. That is the GPU speed variation of `docs/adr/0020` section 10.7. Its cause (thermal or power state) is not measured here. The planner's calibration is taken once per process, so it cannot follow that variation. The same is true of the direct dense CPU/CUDA choice since Phase 10.

**Owner decision (2026-10-09):** keep item 4b and reset criterion 4 to at least 1.2× on at least three of the five circuits in each run, with no circuit more than 10% slower. Both gate runs meet the reset criterion. Following the GPU's state during a run, for example by recalibrating, is not part of Phase 17.

---

## 11. Gate evidence (2026-10-09)

Criteria from `docs/BUILD_PLAN.md`, Phase 17, as reworded and reset by the owner (sections 9 and 10.3).

1. **Backend selection (item 1), `8acebe0`, extended to Factorized plans in `e214c27`.**
   - Tests for each entry point:
     - `RunOptions::backend`: `BackendRequestTest`, `CudaBackendRequestTest`, `CudaBackendRequestDeviceTest`;
     - planner: `CudaBackendSelectionTest.Forced*`, `FactorizedRegionBackendTest.Forced*`;
     - Python and Qiskit: `test_backend_selection.py`;
     - CLI: `CliGoldenTest.*Backend*`.
   - The executed region's backend is asserted on a real device (`EachForcedBackendIsTheOneThatExecutes`).
   - Refusals:
     - no CUDA build, no device, or a noisy circuit: `UnsupportedFeature`;
     - does not fit: `ResourceExceeded`;
     - Factorized with a non-dense region: infeasible with the region named.
   - A forced backend survives a replan (`AForcedCudaOverrunIsRefusedNotMovedToTheCpu`).
   - With both runs dense, forced CPU and forced CUDA give bitwise-identical statevectors and counts (`ForcingABackendNeverChangesTheStatevector`, `DenseCountsAreIdenticalOnEitherForcedBackend`, `AFactorizedPlanOnTheDeviceGivesIdenticalCounts`).
2. **Qiskit time budget (item 2), `d49d1c5`.** `test_time_budget.py`:
   - a met budget gives the same counts;
   - an unmeetable one raises `ResourceExceededError`;
   - invalid values raise `InvalidCircuitError`;
   - the replan record reaches `metadata["menos_replans"]`;
   - no metadata is added without a replan.
3. **CLI time budget (item 3), `199efd4`.** `CliGoldenTest`: a budget on `explain` and on `run` (same counts), a refusal, and each invalid form (`abc`, `5s`, `0`, `-1`, `nan`, `inf`, empty, missing).
4. **Per-region selection (item 4).**
   - Step 4a is recorded in section 10.
   - The owner built 4b for Factorized only (10.1); implementation in 10.2.
   - The two gate runs pass the reset criterion (10.3): `gate_criteria.py` exits 0, with 3 of 5 circuits at ≥ 1.2× in each run and none more than 10% slower.
   - Per-region choices appear on the `region` lines of `explain()`.
   - Results are unchanged (the factorized device test above).
   - Cut plans keep the CPU: measured, not adopted.
5. **Defaults unchanged.**
   - Every pre-existing test and every CLI golden passes unmodified, except the owner's ruling on `test_the_qiskit_backend_still_exposes_no_time_budget` (section 9).
   - `BackendRequestTest.AnUnsetBackendLeavesTheReportByteIdentical` asserts the unforced report byte for byte.
   - The fix found along the way: a non-finite or non-positive time budget is now refused (`ReplanTest.ANonFiniteOrNonPositiveBudgetIsRefused`). Before, NaN was silently ignored.
6. **`scripts/verify.sh` exits 0 on `1041893`** (WSL clone, `UV_PYTHON_PREFERENCE=only-managed`, build parallelism 12 via `OMP_NUM_THREADS`):

   | Stage | Result |
   |---|---|
   | dev / release | 631/631 each |
   | pytest | 259 passed, 1 deselected |
   | Wheel smoke test | passed |
   | cuda-dev / cuda-release (CUDA 13, above the WSL driver's CUDA 12.7) | 686/686 each, GPU tests skipping, as in earlier phases |
   | cuda12-dev / cuda12-release | 686/686 each, GPU tests executing |

   In `cuda12-release`, the following ran and passed on the GPU: `FactorizedRegionBackendTest` 7/7, `CudaBackendRequestDeviceTest` 4/4, `CudaSpecialisedKernelsTest` 5/5 and `CpuCudaDifferentialTest` 11/11.

   The skips in the cuda12 stages:
   - `CudaExecutionMonitorTest.TenAbandons…` and, in dev, `WallTimeReportTest.TwentyFourQubits…`, both of which Phase 16 had;
   - the new `CudaBackendRequestTest.ForcedCudaWithoutAUsableDeviceIsUnsupported`, which skips by design when a device is present and executes in the cuda-dev/cuda-release stages.

   The documentation site was not built (Node is not installed).

   The first full run at 22 jobs was stopped by the host for low memory. The next run failed one stage, clang-tidy on the new benchmark (`bugprone-easily-swappable-parameters`), which is fixed in `1041893`.
7. **Documentation.**
   - `README.md`;
   - `docs/reference/python-api.md`, `qiskit-backend.md`, `configuration`-relevant pages;
   - `docs/build/menos-backend.md`, `command-line.md`;
   - `docs/runtime/time-budgets.md`;
   - `docs/planning/backend-selection.md`;
   - `docs/engines/cuda.md`;
   - `docs/reference/limitations.md` and the changelog;
   - this ADR and `BUILD_PLAN.md`.

   New symbols and their callers:
   - `RunOptions::backend`, `Plan::requested_backend`, `JobSpec::backend` and `Result.replans` have production callers (the planner, `explain()`, the adapters, the CLI) and tests;
   - `menos_cuda_factorized_backend_benchmark` is built by `verify.sh`.

### 11.1 Owner acceptance (2026-10-10)

The owner accepted the Phase 17 gate on 2026-10-10 on the evidence above, with the rulings already recorded in sections 9 and 10.3 (criterion 1 reworded, criterion 4 reset, the Phase 11 test under criterion 5 replaced). No further ruling was made. The commits after `1041893` change documentation only. Phase 17 is COMPLETE.
