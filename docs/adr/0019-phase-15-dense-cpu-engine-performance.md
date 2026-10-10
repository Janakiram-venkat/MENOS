# ADR 0019: Phase 15 — dense CPU engine performance

## Status

**Accepted. Approved, implemented, and the owner accepted the Phase 15 gate on 2026-10-08** (sections 10.9–10.10). The owner started Phase 15 on 2026-10-07. Branch: `phase-15-dense-cpu-performance`, from `main` at `d78e3e4`. This ADR is the design step that `docs/BUILD_PLAN.md` Phase 15 requires first (threading model, determinism of reductions, fusion yes or no). The owner answered section 9 on 2026-10-07 (answers recorded there) and approved the step-1 plan the same day. Step 1 (benchmark hygiene and the baseline) is implemented and measured; section 10 is the baseline. On 2026-10-08 the owner set the target (`docs/BUILD_PLAN.md`, Phase 15 gate: N = 4, M = 5, plus three conditions), decided the `max_threads` semantics, the time-budget behaviour under a thread cap and the OpenMP dependency (sections 3.3 and 3.4), and approved the steps 2–5 implementation plan.

The phase has two owner checkpoints, not one, because the BUILD_PLAN does not let the gate's numeric target be set until the step-1 baseline exists:

1. Section 9 answered, then plan approved → implement **step 1 only** (benchmark hygiene) and measure the baseline.
2. The owner sets the speed-up target from that baseline and the gate is written into `docs/BUILD_PLAN.md` → implement steps 2–5.

The labels follow `docs/adr/0014`–`0018`: **[MEASURED]**, **[MENOS]** (a proposed design decision), **[REJECTED]** and **[OPEN]** (an owner decision, section 9).

---

## 1. Context

Facts about the existing code that the design depends on (read on 2026-10-07 at `d78e3e4`):

1. `kernels::cpu::apply_matrix` (`src/kernels/cpu/dense_kernels.cpp`) is the only CPU gate kernel. For every one of the 2^(n−k) amplitude groups it expands the group's index bit by bit over all n positions, gathers 2^k amplitudes into a heap vector, multiplies by the dense 2^k × 2^k matrix, and scatters back. It is single-threaded. There are no 1- or 2-qubit special cases.
2. The kernel is shared. Besides `DenseEngine` (through the forwarder `engines::dense::apply_matrix`), it is called by `DensityMatrixEngine` (twice per gate, on the vectorised ρ) and `TrajectoryEngine` (gates and Kraus branches). Any change to it changes those engines' speed too, and their existing tests must keep passing unmodified.
3. `DenseEngine::apply_operations` calls `ir::generate_matrix` for every `GateOp`, allocating a fresh matrix per gate, and calls `monitor->at_safe_boundary(completed)` after every operation (Phase 11, `docs/adr/0015`).
4. Two kinds of pass over the state are **reductions**, each a single serial loop in index order: `expectation_of_term` (one per observable term) and the sampler's cumulative sum of `std::norm` in `src/engines/dense/sample.cpp` (and `CumulativeDistribution` for non-sole regions). Sampled counts depend on the exact floating-point cumulative sums, so changing their summation order can change counts under a fixed seed.
5. The gate kernel itself is **independent per amplitude group**: each group is read and written by exactly one iteration. Splitting the groups across threads therefore gives bitwise the same state as the serial loop, whatever the thread count, provided each group is computed with the same arithmetic.
6. The cost model charges 2^(m+k) work units per k-qubit gate (`DenseEngine::estimate`). Calibration (`docs/adr/0014` section 3) converts work units to seconds with a rate measured on a 13-qubit (128 KiB, cache-resident) H+CX circuit, and floors the estimate by `peak_bytes / bandwidth`, where bandwidth is a **single-threaded** `memcpy` measurement.
7. The dashboard (`tools/dashboard/menos_dashboard.py`) times one circuit family (H layer, T on qubit 0, brick-work CX; `LAYERS` layers) against `qiskit.quantum_info.Statevector`, with one warm-up and the **median of 3** repeats (default `--repeats 3`). It records CPU model, platform, versions and git commit. It records no spread and no power state. `menos_dense_gate_application_benchmark` times one generic 2-qubit matrix at 16–26 qubits.
8. The `dev` preset builds with ASan + UBSan. No thread sanitizer preset exists. No threading library is a dependency today (`docs/adr/0001`).
9. The dashboard figures in `docs/BUILD_PLAN.md` (0.98× and 1.52× MENOS/Qiskit at n = 22) were measured on an Intel Core Ultra 7 155H. That is the owner's current machine (Windows 11 with WSL Ubuntu, Intel Arc integrated graphics and an RTX 4060 Laptop GPU). The Phase 11 verification record in `CLAUDE.md` names a different GPU (RTX 3050). The owner chose the Core Ultra 7 155H as the Phase 15 gate machine (Q1).

## 2. Step 1: benchmark hygiene — [MENOS], parameters decided (Q1)

Before any kernel changes:

- **Repeats and spread.** The dashboard's default repeats rise from 3 to 10 (owner, Q1), and every point reports median, minimum, maximum and interquartile range for MENOS wall, MENOS execute and Qiskit wall. The rendered page shows the spread beside the median. The ratio MENOS/Qiskit is reported with the range implied by the two spreads, not as a single number.
- **Power and machine state.** `environment()` additionally records: AC or battery (`/sys/class/power_supply/*/online` on Linux; `GetSystemPowerStatus` through `ctypes` on Windows; "unknown" otherwise, never guessed), the CPU frequency governor where `/sys` exposes it, logical and physical core counts, and whether the process ran under WSL. A field that cannot be read is recorded as `null`, never omitted.
- **The dense benchmark** gets `->Repetitions(N)->ReportAggregatesOnly(true)` so Google Benchmark reports mean, median and standard deviation, plus a 1-qubit case beside the existing 2-qubit case, because step 3 adds a separate 1-qubit kernel.
- **Baseline record.** The baseline run (both tools, on the Core Ultra 7 155H in the WSL clone `~/MENOS`, release build, on AC power, on `main`'s kernel) is committed as JSON under `docs/benchmarks/phase-15-baseline/` and summarised in section 10 of this ADR. The gate is measured against that file, not against the BUILD_PLAN's older figures.

No MENOS library code changes in step 1.

## 3. Step 2: multi-threaded gate application — mechanism and policy decided (Q2, Q3)

### 3.1 Mechanism

Two options:

- **(a) OpenMP** — `#pragma omp parallel for schedule(static)` over the amplitude groups. Least code; GCC and Clang both ship it; `find_package(OpenMP)` in CMake. Costs: a new dependency (amends `docs/adr/0001`); libgomp is loaded into the Python process beside NumPy's and Aer's own thread pools, so oversubscription in the Python tests must be checked; ASan/UBSan work with libgomp, but sanitizer-clean runs must be shown.
- **(b) A small hand-rolled pool** (`std::jthread` workers, static contiguous partition, a barrier per gate). No dependency and full control over thread lifetime inside the Python extension, at the cost of roughly 150–250 lines of concurrency code MENOS must own and test.

**Decided (owner, Q2), then revised on measurement.** The owner first chose (a) OpenMP (2026-10-07), and it was implemented (`18530af`). On 2026-10-08, measuring the threading cut-off on the gate machine showed that OpenMP's default waiting policy is unusable there, and the owner switched to **(b), a MENOS-owned pool** (`src/kernels/cpu/thread_pool.h`).

The measurement [MEASURED]: a 2-qubit gate, median µs per call over 7 batches, 22 hardware threads (WSL), release build, on AC:

| | n = 12 | n = 16 | n = 20 | n = 22 |
|---|---|---|---|---|
| 1 thread (step-3 kernel) | 78 | 280 | 3,870 | 16,670 |
| 22 threads, OpenMP default policy | 7,000–25,000 | 5,000–10,600 | 4,800–12,800 | 6,500–23,000 |
| 22 threads, `OMP_WAIT_POLICY=passive` | 399 | 388 | 1,491 | 4,989 |

With the default policy, idle OpenMP workers spin between parallel regions. On 22 virtual CPUs under WSL, the host deschedules spinning vCPUs, and every gate then waits 5–30 ms for them; smaller teams (8–11 threads) stalled intermittently too. With the passive policy (workers sleep) the stalls vanish. But that policy can be set only through `OMP_WAIT_POLICY`, which the OpenMP runtime reads once when it loads, before any MENOS code runs, so MENOS could not choose it for its users.

The pool's workers block on a C++20 atomic wait (a futex on Linux), so they sleep by construction, whatever the environment. The pool splits a range into contiguous static chunks that depend only on the element count and the team size; the calling thread runs chunk 0. If the pool is already in use by another thread, or the process is a fork of the one that started it, the call runs on the calling thread alone (same result, less speed). It replaces OpenMP entirely; the OpenMP dependency is withdrawn (`docs/adr/0001`).

### 3.2 Determinism [MENOS]

Gate application is parallelised only over independent amplitude groups (fact 5), with the same per-group arithmetic as the serial path. The result is therefore **bitwise identical** to the single-threaded kernel for every thread count. A test asserts exact equality (not tolerance) between 1 thread and the maximum on a randomised circuit set, for the dense, density-matrix and trajectory engines.

### 3.3 Thread-count policy

Threading a small state costs more than it saves. The serial/parallel cut-off is a **measured** figure (the qubit count at which the threaded kernel first beats the serial one on the gate machine), recorded in section 10, never a guessed constant. Decided (owner, Q3): the default thread count is the hardware thread count (`std::thread::hardware_concurrency()`), and a new optional public `RunOptions::max_threads` (C++ and Python, default: all hardware threads) lets the caller cap it. Semantics, decided by the owner on 2026-10-08:

- `max_threads` is a **cap**. Unset means the hardware count, `max(1, std::thread::hardware_concurrency())`. A value above the hardware count uses the hardware count, the cap simply not being reached. The engine may use fewer threads than the cap, for example below the cut-off above.
- `max_threads = 0` is refused with `InvalidCircuit`, like any other invalid option; it is never reinterpreted.
- `OMP_NUM_THREADS` is **ignored** (owner). With the MENOS pool that holds trivially: no OpenMP runtime is involved, so only `RunOptions` controls MENOS's threads, and two processes with the same options run the same configuration.
- The resolved count is not printed by `explain()`. Changing that output would break the CLI golden files, which Phase 15 must leave unmodified. Results do not depend on it (section 3.2).

### 3.4 Budgets

- **Memory:** the in-place kernel allocates no per-thread state proportional to 2^n; per-thread scratch is 2^k amplitudes. `DenseEngine::estimate`'s `peak_bytes` is unchanged.
- **Time:** calibration (fact 6) must measure the build as it will run. Two consequences, both measured in step 5 rather than assumed: the 13-qubit calibration circuit may sit below the threading cut-off, so the work-unit rate would describe the serial path while large circuits run threaded; and the single-threaded `memcpy` bandwidth floor understates a multi-threaded sweep. Either can make `time_budget_seconds` refuse jobs that would fit. The owner permits step 5 to change the calibration method (Q6), section 7.
- **A capped thread count and a time budget** (owner, 2026-10-08). The planner keeps one calibrated rate, measured with the default thread count. A job that caps `max_threads` below the default and also sets `time_budget_seconds` is planned with that rate, which is optimistic for it. If the capped run is slower and crosses its budget, the Phase 11 monitor abandons it and replans, or throws `ResourceExceeded` (`docs/adr/0015`). No constant scaling the rate by thread count is introduced. This is stated in `RunOptions`' documentation.
- **Build dependency.** OpenMP was made a required dependency on 2026-10-08 and withdrawn the same day with the switch to the MENOS pool (section 3.1). The pool needs only the platform thread library (`find_package(Threads)`), which every supported toolchain provides. There is still exactly one build configuration.

## 4. Step 3: specialised 1- and 2-qubit kernels — [MENOS]

- `apply_matrix` keeps its signature and dispatches on `operands.size()`: k = 1 and k = 2 go to new kernels that iterate the group index with bit insertion (two or three shifts and masks, no per-bit loop), hold the matrix and the 2 or 4 amplitudes in registers, and allocate nothing. k ≥ 3 stays on the generic path.
- The current body is kept, renamed `apply_matrix_generic`, and remains the oracle. Tests compare the specialised kernels with it on random states and random unitaries, on every operand position and order (including k = 2 with the operands reversed and non-adjacent), by **exact (bitwise) equality**. Decided (owner, Q4, revised 2026-10-07): each output amplitude is computed exactly as the generic kernel computes it, starting from zero and adding the products `matrix[out][in] * a[in]` in increasing `in` order, with no fused multiply-add. The speed-up comes from the indexing (no per-bit loop, no gather vector), not from reordering arithmetic. Reason for the revision: `docs/adr/0014` section 10 makes CPU and CUDA statevectors bitwise identical because both sum in the same order, so CPU and CUDA counts agree by construction for a given seed, and `tests/api/cpu_cuda_differential_test.cpp` asserts it element by element. A tolerance-only CPU kernel would break that property, which the performance-roadmap invariants require to hold. The build must therefore not enable FMA contraction for these kernels (no `-march=native` or `-mfma` without `-ffp-contract=off`); a test pins this.
- `DenseEngine::apply_operations` stops allocating a matrix per `GateOp` only if the step-1 profile shows that allocation in the bottleneck (no speculative changes).

## 5. Reductions — decided (Q5)

Decided (owner, Q5): **expectation sums and the sampling cumulative distribution stay serial in this phase.** They are one pass each against one pass per gate, so on the dashboard family they are a small share of runtime, and leaving them serial keeps every count and expectation bitwise unchanged with no ADR exception needed. Step 1's profile records their share; if it is material, a deterministic parallel reduction (a fixed number of chunks independent of the thread count, combined in chunk order) is proposed in an amendment, with the golden-file and count changes it causes listed before it is adopted.

## 6. Gate fusion — [DEFERRED] (Q7)

Fusion (combining neighbouring gates into one matrix before the sweep) is **deferred out of Phase 15** (owner, Q7). The reasons, kept for whoever re-evaluates it:

- The BUILD_PLAN requires a benchmark showing the bottleneck before an optimisation. Whether the threaded, specialised kernel is still sweep-count bound is unknown until steps 2–3 are measured.
- It interacts with two frozen invariants. Fewer operations means fewer Phase 11 safe boundaries, so the monitor's granularity and its `completed` counts change; and the cost model's per-gate 2^(m+k) charge would need a fused-circuit form, which is a planner change.
- A fused k-qubit block is denser (k = 3–5 matrices), so it would run on the generic path that step 3 does not speed up.

A later phase that takes fusion up needs: equality to the unfused circuit within tolerance, an `explain()` line, Phase 11 boundary semantics stated, and the cost model's treatment of fused blocks.

## 7. Step 5: recalibration and planner effects — [MENOS]

The owner permits the calibration method of `docs/adr/0014` section 3 to change (Q6) where step 5's measurements show that it no longer describes the threaded engine: for example a throughput circuit above the threading cut-off, or a multi-threaded bandwidth figure. Any such change is written as an amendment to this section with the before/after measurements that justify it, before the code changes. After steps 2–3, calibration is re-run on the gate machine in dev and release, and section 10 records: the old and new work-unit rate and bandwidth; the `cost_model_correlation_benchmark` correlation before and after (the specialised kernels change the real cost of k = 1, 2 gates relative to 2^(m+k), which may weaken it); and every planner decision that changes, measured with `cuda_crossover_benchmark` (the CPU/CUDA crossover, currently n = 14) and the existing planner golden tests. A changed planner decision is recorded; a changed golden file is listed explicitly in the commit and in section 10.

### 7.1 Outcome (2026-10-08): the calibration method is kept — owner decision

Measured at `24d082f`/`cd4c074` (section 10.8). The 13-qubit throughput circuit stays below the threading cut-off, so the calibrated rate describes the single-threaded path. The planner's estimates match single-threaded runs to 0.69–1.05× but are 2.5–3.1× **pessimistic** for threaded runs at n ≥ 20. Backend choices for n ≥ 18 are still right, because CUDA is genuinely faster there. The owner chose to **keep the calibration method unchanged** and record the effect:

- Estimates err only on the safe side. A time-budgeted large CPU job may be refused when it would fit; none is accepted that the estimate says fits but threading makes overrun.
- **Deferred:** a two-rate cost model (a single-threaded and a threaded work rate, with gates at or above the cut-off charged at the threaded rate). It would change the cost model of the dense, density-matrix and trajectory engines and the time-budget behaviour, and needs its own ADR.
- No calibration code changed, apart from the request carrying the default thread count, which changes nothing at 13 qubits because that circuit is below the cut-off.

## 8. Risks and required measurements

| Risk | How it is caught |
|---|---|
| A thread count changes a result | Exact-equality test, 1 thread vs maximum, three engines (section 3.2) |
| Oversubscription in Python (Aer, NumPy and MENOS thread pools) | Python test suite timing before/after; thread cap honoured |
| Sanitizer noise from libgomp | `dev` preset must pass clean; any suppression is documented and narrow |
| Calibration describes the serial path | Step 5 measures rate at and above the cut-off |
| Speed-up indistinguishable from noise | Step 1 spread; gate requires the improvement to exceed it |
| Laptop power/thermal state skews a run | Recorded per run (section 2); gate runs on AC |

## 9. Owner decisions needed

- **Q1 — Gate machine and hygiene parameters.** Which machine is the Phase 15 gate measured on (the Core Ultra 7 155H that produced the BUILD_PLAN figures, or the WSL verification machine)? Native Linux, Windows or WSL? Repeats per point (proposal 10) and the spread statistic (proposal: min–max and IQR beside the median)?
- **Q2 — Threading mechanism.** OpenMP (proposed) or a hand-rolled pool? OpenMP adds a dependency, amending `docs/adr/0001`.
- **Q3 — Thread-count policy and API.** Default all hardware threads (proposed)? Add `RunOptions::max_threads` (public C++ and Python API, proposed), or keep thread count internal?
- **Q4 — Specialised kernels: tolerance or bitwise equal to the generic oracle?** Originally proposed: tolerance. Revised proposal: bitwise, because of the CPU/CUDA identity in `docs/adr/0014` section 10 (section 4).
- **Q5 — Reductions.** Keep expectation sums and the sampling CDF serial in Phase 15 (proposed)?
- **Q6 — Calibration under threading.** Accept that step 5 may change the calibration method (circuit size above the cut-off, multi-threaded bandwidth) through an amendment to this ADR, or keep `docs/adr/0014`'s method fixed and only re-run it?
- **Q7 — Gate fusion.** Defer out of Phase 15 (proposed), or in scope as a post-measurement amendment?

### Owner answers (2026-10-07)

1. **Q1:** the gate machine is the Intel Core Ultra 7 155H (the owner's machine, measured in its WSL clone). 10 repeats per point, with min–max and interquartile range shown beside the median.
2. **Q2:** OpenMP; revised on 2026-10-08 to a MENOS-owned thread pool, after measurement showed OpenMP's default waiting policy stalling every gate by 5–30 ms on the gate machine (section 3.1).
3. **Q3:** all hardware threads by default; add `RunOptions::max_threads` (public, C++ and Python).
4. **Q4:** first answered "within `common/tolerance.h`". Revised by the owner the same day, once the conflict with the CPU/CUDA bitwise identity (`docs/adr/0014` section 10) was pointed out: the specialised 1- and 2-qubit kernels are **bitwise identical** to the generic kernel.
5. **Q5:** expectation sums and the sampling CDF stay single-threaded.
6. **Q6:** step 5 may change the calibration method, by amendment with measurements.
7. **Q7:** gate fusion is out of Phase 15.

The numeric speed-up target is **not** asked here. Per `docs/BUILD_PLAN.md`, the owner sets it after the step-1 baseline (section 10).

## 10. Baseline and acceptance evidence

### 10.1 Step 1: baseline (2026-10-07/08) — [MEASURED]

**Status: step 1 is implemented and measured. The owner sets the Phase 15 speed-up target from this section, and the gate is then written into `docs/BUILD_PLAN.md`.**

Step 1 code: commits `7e0f374`, `18fec77`, `c85ee35` and `b641445` (dashboard spread, power state read before and after the sweep, page rendering), and `e6195c1`, `870ef19` and `4790bdc` (dense benchmark repetitions, 1-qubit case, expectation and sampling passes, every repetition kept in the JSON). No MENOS library code (`src/`, `include/`) changed; the library measured is `main`'s.

**Machine and conditions.** Intel Core Ultra 7 155H, WSL2 Ubuntu (kernel 6.18.33.2-microsoft-standard-WSL2), GCC 15.2.0 `-O3` (`release`/`bench` builds; the Python extension is a Release build), Python 3.14.7, Qiskit 2.5.2. The CUDA crossover used `cuda12-release` (GCC 13.4.0) on the RTX 4060 Laptop GPU. Windows power scheme "Balanced" throughout. WSL reports 22 logical and 11 "physical" CPUs, which is its virtual topology: the 155H has 16 cores (6 P, 8 E, 2 LP-E) and 22 threads. No CPU frequency governor is exposed in WSL.

**Data.** `docs/benchmarks/phase-15-baseline/`:
- `run-c/`, `run-d/`: two complete runs (crossover benchmark, dashboard sweep n = 4–24 step 2 with 10 repeats, dense benchmark), at commit `8b34019`, clean tree.
- `dense-e/`, `dense-f/`: two further dense-benchmark runs at `4790bdc`, with every repetition in the JSON. The `run-c`/`run-d` dense JSON holds aggregates only (mean, median, standard deviation, cv) because `ReportAggregatesOnly` also stripped the file output, which `4790bdc` fixes. The dense figures below are therefore from E and F.
- `profile/`: the perf profile (section 10.4).
- `scripts/`: the scripts exactly as run. Their paths point to the session scratchpad they ran from.

Every run checked that the machine was on AC at its start, between tools and at its end, and refused to continue otherwise (`power.log` in each run directory).

**Runs excluded, and why.** Run A (2026-10-07 13:21, before the start-of-run power check existed) agrees with C within 3% on the dashboard, but its power state at the start is unverified, so it is not used. Run B ran on battery: the end-of-run power record showed `ac_online: false`, calibration throughput was 2.07 × 10⁸ against 3.09 × 10⁸ work units/s, and every timing was about 1.5× slower. A first run D lost AC part-way through its dashboard sweep, and the guard stopped it (its 24-qubit MENOS median was 73.7 s against about 50 s on AC). The run D used here is the repeat, on AC throughout.

### 10.2 Dashboard: MENOS against Qiskit (end-to-end wall time, seconds)

Median (min–max) of 10 repeats after one warm-up. The circuit has 6 layers of H on every qubit, T on qubit 0, and CX on alternating pairs. Statevector output; the planner chose the CPU at every n (the Python extension is a CPU build). Every point agrees with Qiskit within 1e-12 (largest difference 5.6e-16).

| n | gates | MENOS, run C | MENOS, run D | D vs C | Qiskit C | Qiskit D | Qiskit ÷ MENOS (C / D) |
|---|---|---|---|---|---|---|---|
| 12 | 111 | 0.00343 (0.00341–0.00540) | 0.00350 (0.00344–0.00682) | +1.9% | 0.00266 | 0.00273 | 0.77 / 0.78 |
| 14 | 129 | 0.0175 (0.0169–0.0249) | 0.0185 (0.0169–0.0237) | +6.0% | 0.00918 | 0.00926 | 0.53 / 0.50 |
| 16 | 147 | 0.129 (0.0974–0.144) | 0.131 (0.0994–0.138) | +1.6% | 0.124 | 0.254 | 0.96 / 1.94 |
| 18 | 165 | 0.498 (0.472–0.533) | 0.500 (0.459–0.509) | +0.4% | 1.65 | 1.71 | 3.32 / 3.42 |
| 20 | 183 | 2.33 (2.23–2.39) | 2.48 (2.38–2.60) | +6.3% | 4.01 | 3.80 | 1.72 / 1.53 |
| 22 | 201 | 10.59 (10.41–10.85) | 11.10 (10.61–11.57) | +4.7% | 15.28 | 13.91 | 1.44 / 1.25 |
| 24 | 219 | 50.23 (47.56–64.16) | 49.49 (48.79–53.02) | −1.5% | 58.47 | 57.54 | 1.16 / 1.16 |

n = 4–10 is dominated by per-call overhead (MENOS 0.07–0.78 ms) and is in the JSON.

**Run-to-run spread.** For n ≥ 12, MENOS's median moves by at most 6.3% between C and D. Qiskit's moves far more (0.124 s against 0.254 s at n = 16), so the Qiskit ratio is the noisiest number here: 0.96× against 1.94× at n = 16 with an unchanged MENOS. MENOS against its own baseline is the more reliable target measure.

### 10.3 Dense benchmark (milliseconds per call)

Median (min–max) of 10 repetitions, runs E and F back to back.

| Benchmark | n = 20 E / F | n = 22 E / F | n = 24 E / F | n = 26 E / F | cv range |
|---|---|---|---|---|---|
| 2-qubit gate (qubits 0, n−1) | 11.6 (11.2–12.1) / 12.0 (11.5–13.3) | 48.8 (46.6–53.4) / 49.5 (47.1–52.9) | 197 (183–205) / 194 (185–201) | 796 (751–876) / 794 (770–813) | 1.4–6.2% |
| 1-qubit gate (qubit n/2) | 13.4 (12.2–14.3) / 13.1 (12.7–13.4) | 56.8 (52.2–62.9) / 55.1 (53.7–57.6) | 231 (215–244) / 234 (231–240) | 980 (933–1070) / 1010 (975–1070) | 1.5–5.8% |
| One Pauli-term expectation pass | 2.17 / 2.04 | 10.5 / 10.4 | 37.8 / 37.3 | 134 / 134 | 1.6–7.9% |
| One sampling pass, 1024 shots | 3.28 / 3.33 | 14.0 / 13.5 | 54.8 / 54.2 | 214 / 203 | 2.2–9.1% |

Medians agree between E and F within 4.0% for the gate benchmarks and within 9.1% for the passes. The 16- and 18-qubit points are in the JSON.

Two facts the design depends on:
- **The generic kernel is slower on a 1-qubit gate than on a 2-qubit gate** at every size (for example 56.8 against 48.8 ms at n = 22), although a 1-qubit gate does half the arithmetic per amplitude. The per-group cost (the bit-by-bit index expansion over all n positions and the gather/scatter, section 1 fact 1) dominates, not the arithmetic. This is the overhead the step-3 kernels remove.
- **The passes that stay single-threaded (Q5) each cost about one gate's sweep** (10.5 ms expectation, 14.0 ms sampling, against 48.8 ms per 2-qubit gate at n = 22), against 201 gates in the 22-qubit dashboard circuit.

### 10.4 Profile — [MEASURED]

`perf record -F 999 -e cpu-clock --all-user` on one release 22-qubit dashboard circuit through the Python API (`profile/run22.py`: 201 gates, 10.64 s wall, 10.56 s engine execute), on AC. Flat self-time, all threads (11,213 samples; `profile/report.txt`):

| Symbol | Share |
|---|---|
| `menos::kernels::cpu::apply_matrix` | 90.71% |
| `blas_thread_server` (`libscipy_openblas64_`) | 6.00% |
| `gc_collect_main` (`python3.14`) | 0.30% |
| everything else (incl. all of libc: `malloc`/`free`) | under 0.3% each; libc 0.43% in total |

- Gate application is the whole of MENOS's cost on this circuit. `ir::generate_matrix` and the per-gate matrix allocation (section 1 fact 3) do not appear, so section 4's condition for removing that allocation is **not met**, and it stays.
- The 6% in `blas_thread_server` is OpenBLAS's worker thread, started by the NumPy/SciPy stack that Qiskit imports, spinning while MENOS runs. It is outside MENOS, but it is direct evidence for the oversubscription risk in section 8: once MENOS runs its own OpenMP threads inside the same Python process, both pools compete for cores. Step 2 measures the Python path with and without `OPENBLAS_NUM_THREADS=1`.

### 10.5 CUDA crossover in these runs

| | CPU n = 22 | CUDA n = 22 | CPU n = 24 | CUDA n = 24 | calibration, host / device (work units/s) |
|---|---|---|---|---|---|
| run C | 10.43 s | 0.397 s | 48.56 s | 1.580 s | 3.541 × 10⁸ / 6.551 × 10⁹ |
| run D | 12.36 s | 0.394 s | 56.06 s | 1.831 s | 2.586 × 10⁸ / 6.556 × 10⁸ |

The device calibration differs **tenfold** between C and D on the same machine, and the host figure by 27%. Both come from the deadline-bounded (~15 ms) microbenchmarks of `docs/adr/0014` section 3.1. A figure that moves this much between identical runs would change the planner's time estimates by the same factor. This is recorded here as an input to step 5 (section 7, Q6), which may change the calibration method; it is not changed in step 1.

### 10.6 What the target can be measured against

- **Measure:** MENOS end-to-end median on the dashboard family at n = 20, 22 and 24 against section 10.2, and the dense benchmark medians against section 10.3.
- **Noise floor:** about 6% run to run on the dashboard and about 4% on the dense gate benchmarks (9% on the passes). A claimed speed-up must clear these by a margin the owner sets.
- **Not a reliable target:** the Qiskit ratio, which moved 2× at n = 16 with MENOS unchanged.

### 10.7 Steps 2–3: kernels, pool and cut-off — [MEASURED]

Gate machine as in section 10.1 (WSL, release build, on AC). The scratch programs are in `docs/benchmarks/phase-15-gate/scripts/`. The C++ ones are stored as `.cpp.txt` records, exactly as run, so they are not built, formatted or tidied with the sources. `cutoff.cpp.txt` refers to the cut-off by an intermediate name it had during the measurement.

**Specialised kernels, one thread** (step 3, `47c59ee`). A 2-qubit gate at n = 20 takes about 3.9–4.4 ms and at n = 22 about 16.6–17.8 ms, against 11.6–12.0 and 48.8–49.5 ms at baseline (section 10.3): about 2.9×. The kernels are bitwise identical to the generic one (`tests/engines/dense/specialised_kernels_test.cpp`), and the CPU/CUDA differential suite (12 tests, GPU executing, `cuda12-release`) still passes element for element.

**OpenMP rejected on measurement** (section 3.1, `18530af` → `69fd8b5`). With OpenMP's default waiting policy, 22 threads stalled 5–30 ms per gate at every size; with `OMP_WAIT_POLICY=passive` the stalls vanished. That policy can only be set through an environment variable read when the runtime loads, so MENOS now uses its own pool.

**Pool dispatch.** The first pool woke each worker separately: about 3 ms per gate at 22 threads under WSL. One broadcast wake-up word (`2a3b377`) brought that to about 200 µs, whatever the team size.

**Threading cut-off: 2^16 groups** (`24d082f`). The value comes from the end-to-end measurement on the dashboard circuit. Engine execute time in ms, median (min–max) of 7 runs:

| n | 1 thread | all threads, cut-off 2^14 | all threads, cut-off 2^16 |
|---|---|---|---|
| 14 | 4.55 / 5.59 | 4.68 (×0.97) | 5.11 (×1.09) |
| 15 | 10.6 / 11.3 | 25.3 (×0.42) | 11.0 (×1.03) |
| 16 | 22.2 / 27.1 | 33.7 (×0.66) | 23.3 (×1.17) |
| 17 | 48.6 / 53.1 | 46.4 (×1.05) | 59.3 (×0.89) |
| 18 | 113 / 119 | 61.8 (×1.83) | 65.7 (×1.81) |
| 19 | 254 / 257 | 90.6 (×2.81) | 95.0 (×2.71) |

The two 1-thread figures are from the two sessions. The ×figures are against the 1-thread run of the same session.

- A kernel-only measurement had first suggested 2^14. Its single-thread figures were inflated when the thread landed on an efficiency core, and on the real circuit 2^14–2^15 groups ran slower threaded.
- A trial at 2^17 (never committed) moved n = 17 to ×1.05 but cut n = 18 to ×1.19, while identical code at n = 15–16 varied by ±10%. So 17's ×0.89 at 2^16 is within the noise, and 2^16 was kept.

### 10.8 Step 5: recalibration and planner effects — [MEASURED]

**Calibration.** The CPU throughput figure rose from 2.59–3.54 × 10⁸ work units/s at baseline (crossover logs of runs C and D) to 0.82–1.18 × 10⁹ (two runs on 2026-10-08), about 3–4×, from the step-3 kernels. Its run-to-run variation is still large (45% between those two runs). The calibration-repeatability test (`CalibrateHardwareTest.RepeatedCalibrationAgreesWithinAnOrderOfMagnitude`) failed once (bandwidth spread 4.65× against the test's limit of 4) in a `verify.sh` run of `58cbb51`, while a parallel build ran beside it. It passed 5 of 5 times on the quiet machine at `5ffe743`.

**Estimate against measurement** (dashboard circuit, statevector, `explain()` with a non-binding time budget, `24d082f`):

| n | estimate | all threads (ratio) | 1 thread (ratio) |
|---|---|---|---|
| 12 | 1.04 ms | 0.99 ms (1.05) | 1.00 ms (1.04) |
| 16 | 22.1 ms | 41.5 ms (0.53)\* | 22.4 ms (0.99) |
| 18 | 99.4 ms | 74.9 ms (1.33) | 130 ms (0.76) |
| 20 | 0.441 s | 0.166 s (2.66) | 0.560 s (0.79) |
| 22 | 1.94 s | 0.774 s (2.51) | 2.81 s (0.69) |
| 24 | 8.47 s | 2.71 s (3.13) | 12.3 s (0.69) |

\*Measured with the 2^14 cut-off, since raised; at 2^16, n = 16 runs single-threaded.

**Cost-model correlation.** `cost_model_correlation_benchmark`: Spearman = 1 (n = 10–20) before and after.

**Forced CPU/CUDA crossover** (`cuda_crossover_benchmark`, `cd4c074`, CPU on all threads):

| | baseline (run C) | now |
|---|---|---|
| statevector: CUDA first faster | n = 14 | between n = 16 (0.63×) and 18 (3.02×) |
| expectation: CUDA first faster | n = 16 | between n = 18 (0.41×) and 20 (1.29×) |
| CUDA advantage, statevector, n = 24 | 28.3× | 1.72× |
| CUDA advantage, expectation, n = 24 | 34.6× | 1.16× |

**Planner decision** (`menos::preview`, dashboard circuit, statevector, `cuda12-release`):

| n | 10–12 | 14 | 16 | 18–26 |
|---|---|---|---|---|
| baseline `58cbb51` | CPU | CUDA | CUDA | CUDA |
| now `cd4c074` | CPU | **CPU** | CUDA | CUDA |

The planner's CPU→CUDA switch moved from n = 14 to n = 16. At n = 16 it still picks CUDA although the CPU is faster there (22 ms against 35 ms). The CPU estimate at n = 16 is accurate (single-threaded, 0.99×), so the error is on the CUDA side, where the device calibration varied tenfold between runs (section 10.5). That is pre-existing and outside Phase 15. No planner golden file changed: every pre-existing test passes unmodified.

**A defect found and fixed.** `EngineExecutionRequest::max_threads` first defaulted to 1, so the crossover benchmark, which builds requests directly, measured the CPU single-threaded (2.74 s at n = 22 against 0.72 s threaded). The default is now the hardware thread count (`cd4c074`), like `RunOptions` and `JobSpec`.

### 10.9 Gate evidence (2026-10-08) — [MEASURED]

**Gate runs.** Gate runs A and B are complete runs (crossover benchmark, dashboard n = 4–24 with 10 repeats, dense benchmark with all-thread and one-thread variants). Run C is the dashboard alone with `OPENBLAS_NUM_THREADS=1`. All three ran at `1332f7e` on the gate machine, release builds, "Balanced" scheme, on AC at every check (`power.log`). Data and scripts are in `docs/benchmarks/phase-15-gate/`; `scripts/gate_criteria.py` computes every figure below from the JSON. At each point the baseline is the **faster** of its two baseline runs (C/D for the dashboard, E/F for the dense benchmark), the stricter comparison.

**A first gate attempt** at `b0d815a` (data in `attempt-1-b0d815a/`) passed criteria 1, 2 and 4 but **failed criterion 3 at n = 4** in both runs (+50% and +30%). The engine itself was faster there (execute 3.9 µs against 5.6 µs). The cost was per-run overhead: `std::thread::hardware_concurrency()` reads a `/sys` file on every call, about 2 µs warm and more when cold.
- In C++, `menos::run` at n = 4 was 1.5–2 µs slower per run than at baseline.
- In the dashboard, with Qiskit runs interleaved, it was 8–14% slower.

`1332f7e` reads the count once per process. In an interleaved dashboard comparison of the baseline and head extensions (three rounds), the head then measured 73–79 µs at n = 4 against the baseline extension's 91–106 µs. The gate runs were then repeated in full.

| # | Criterion | Evidence | Result |
|---|---|---|---|
| 1 | Dashboard ≥ 4× at n = 22, 24 | A: 15.18×, 16.14×. B: 15.21×, 16.30× | **pass** |
| 2 | Dense 1q/2q gate ≥ 5× at n = 22, 24, 26 (all threads) | 2-qubit 10.24–13.11×, 1-qubit 18.86–25.62× (both runs) | **pass** |
| 3 | No dashboard point n = 4–18 > 10% slower | Worst: n = 4, +7.7% (A) and +9.5% (B); every other point 6.9–72.6% faster | **pass, narrowly at n = 4** |
| 4 | One thread ≥ 2× at n = 22, 24, 26 | 2-qubit 2.84–2.98×, 1-qubit 5.62–6.25× (both runs) | **pass** |
| 5 | Results unchanged | Specialised kernels bitwise equal to the generic one; thread counts bitwise equal (kernels; dense, density-matrix and trajectory engines through `run()`; Python); CPU/CUDA differential 12/12 on the GPU; dev (ASan/UBSan) and release suites pass; no C++ test and no golden file modified. One pre-existing **tool** test, `python/tests/test_dashboard.py`, was extended for the step-1 dashboard schema (see below) | **pass, one flagged item** |
| 6 | Recalibration recorded | Sections 7.1 and 10.8. The crossover JSON of runs A and B records the calibration at `1332f7e` | **recorded** |
| 7 | OpenBLAS interaction measured | Below | **recorded** |
| 8 | `scripts/verify.sh` passes; no Phase 16 work | Section 10.10: exit 0 on `a01052a`; no CUDA source changed on the branch | **pass** |

**Criterion 5, the flagged item.** `test_sweep_agrees_with_qiskit` in `python/tests/test_dashboard.py` predates Phase 15. Step 1, an approved plan, changed the dashboard's results schema, and that test now runs 3 repeats instead of 1 and also checks the new spread fields. It tests the benchmark tool, not any MENOS result. Whether this counts against "every pre-existing test passes unmodified" is the owner's decision.

**Criterion 7.** MENOS end-to-end medians in seconds, all threads (runs A / B) and with `OPENBLAS_NUM_THREADS=1` (run C):

| n | 14 | 16 | 18 | 20 | 22 | 24 |
|---|---|---|---|---|---|---|
| A / B | 0.0050 / 0.0061 | 0.0473 / 0.0484 | 0.136 / 0.161 | 0.240 / 0.235 | 0.698 / 0.696 | 3.07 / 3.04 |
| `OPENBLAS_NUM_THREADS=1` | 0.0050 | 0.0268 | 0.0771 | 0.228 | 0.784 | 2.73 |

- With OpenBLAS's pool limited to one thread, MENOS is 1.8× faster at n = 16–18, where the dashboard runs a NumPy/Qiskit computation between MENOS calls and OpenBLAS's spinning workers compete with MENOS's pool. At n = 20–24 the difference is within ±12%.
- Qiskit itself is also faster with it (for example 0.80 s against 0.27 s at n = 18).
- MENOS does not set the variable for the caller (it is process-wide and belongs to the application); the README notes it.

### 10.10 Verification run (2026-10-08)

`scripts/verify.sh` on `a01052a` (the branch head), in the WSL clone with `UV_PYTHON_PREFERENCE=only-managed`, 08:49–10:20 UTC: **exit 0** ("verify.sh: all stages passed").

- Format check and clang-tidy (C++ targets, benchmarks, `menos._core` bindings, CUDA targets, CUDA benchmarks) clean.
- dev 619/619 and release 619/619 (dev skips the 24-qubit wall-time test by design).
- pytest 236 passed, 1 deselected (the wheel smoke test, which then passes in its own stage); wheel smoke 1 passed.
- cuda-dev 652/652 and cuda-release 652/652 (CUDA 13.3.73; GPU tests skip, driver 566.14).
- cuda12-dev 652/652 and cuda12-release 652/652 (CUDA 12.4.131, on the GPU). `CudaExecutionMonitorTest.TenAbandonsOfAnOversubscribingTotalNeverExhaustTheDevice` reports Skipped in both.

**No Phase 16 work.** `git diff main...HEAD` touches no CUDA source or kernel; the changed sources are the CPU kernels, the thread pool, the dense, density-matrix and trajectory engines, `RunOptions`/`JobSpec` and the Python binding.

**Not verified.** The docs site was not built (node/npm is not installed in WSL), so the new documentation's sidebar links were not link-checked; `test_docs_snippets.py` runs its Python snippets.

### Gate decision

The owner accepted the Phase 15 gate on 2026-10-08, on the evidence in sections 10.9 and 10.10. The extension of `python/tests/test_dashboard.py` flagged under criterion 5 tests the benchmark tool, not a MENOS result, and the owner ruled it does not count against the criterion. Phase 15 is COMPLETE.
