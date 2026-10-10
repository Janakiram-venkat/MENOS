# ADR 0020: Phase 16 — specialised CUDA kernels

## Status

**Accepted; Phase 16 COMPLETE (2026-10-09).** The owner accepted the Phase 16 gate on 2026-10-09 on the evidence in section 13, with a recorded ruling: the owner ruled that the one miss, criterion 1 at n = 8 in gate run b (1.39× against 1.4×, inside that point's spread and with the GPU throttled in that run), does not count against the gate. The owner answered every question of section 9 on 2026-10-08 (answers recorded there; Q4 after the step-1 profile), approved the implementation plan for steps 2–4, and set and reset the gate targets (section 11). Branch `phase-16-specialised-cuda-kernels`, merged to `main` in `c6b9c87`. This ADR amends nothing in `docs/adr/0014`.

The labels follow `docs/adr/0014`–`0019`: **[MEASURED]**, **[DERIVED]** (arithmetic on measured figures, not itself measured), **[MENOS]** (a proposed design decision) and **[OPEN]** (an owner decision, section 9).

As in Phase 15, the gate's numeric targets are not proposed here. `docs/BUILD_PLAN.md` says the Phase 16 gate is written at kickoff. This ADR proposes that it is written from a fresh step-1 baseline, the way Phase 15's was (section 9, Q6).

---

## 1. Context

### 1.1 What is shipped [MEASURED, `docs/adr/0014` sections 10 and 16.3]

`src/kernels/cuda/dense_kernels.cu` has one gate kernel, `apply_matrix_kernel`, generic over operand count k = 1..5. It is one thread per outer index. Each thread expands its index bit by bit, gathers 2^k amplitudes into local arrays, multiplies by the matrix in `__constant__` memory and scatters. Every product is summed in the same order as the CPU primitive and the build uses `-fmad=false`, so CPU and CUDA statevectors are **bitwise identical**. CPU and CUDA counts are identical by construction because of this.

For each gate, the host `apply_matrix` makes two synchronous `cudaMemcpyToSymbol` calls (matrix and operands), one launch and one `cudaDeviceSynchronize`. `CudaDenseEngine` (`src/engines/dense_cuda/cuda_dense_engine.cpp`) depends on that synchronisation: the Phase 11 monitor reads the wall clock at each gate boundary and assumes the gate has finished on the device (comment at line 49).

`docs/adr/0014` section 16.3 records a hand-specialised H and CX probe (scratch code, not in the repository). On the RTX 3050 Laptop GPU it reached **57–134× over the CPU at n = 22**, crossover n ≈ 10. The shipped generic kernel reached 14–16× at n = 24, crossover n = 12.

### 1.2 Where things stand after Phase 15 [MEASURED, `docs/benchmarks/phase-15-gate/run-a/crossover.log`, `1332f7e`]

The machine is the gate machine: RTX 4060 Laptop GPU (CC 8.9, 24 SMs, 8 GiB) in WSL, with the `cuda12-release` build and an Intel Core Ultra 7 155H. The circuit is 6 layers of (H on all qubits, T on q0, CX on alternating pairs). Times are end-to-end `execute()`, transfers included.

| n | 4 | 12 | 14 | 16 | 18 | 20 | 22 | 24 |
|---|---|---|---|---|---|---|---|---|
| CPU (s), threaded, Phase 15 | 0.000005 | 0.00096 | 0.0044 | 0.0246 | 0.0809 | 0.154 | 0.705 | 2.77 |
| CUDA (s), generic kernel | 0.0040 | 0.0090 | 0.0107 | 0.0184 | 0.0348 | 0.114 | 0.401 | 1.58 |
| CUDA speed-up (statevector) | 0.00× | 0.11× | 0.41× | 1.34× | 2.33× | 1.35× | 1.76× | 1.75× |

Three findings follow.

1. **The GPU's lead is now small.** Phase 15 made the CPU 4–16× faster, and the CUDA path did not change. At n = 22–24 the GPU is 1.4–1.8× faster than the CPU, against 28× in the BUILD_PLAN roadmap figure. The planner's crossover moved from n = 14 to n = 16 (`docs/adr/0019` section 10.8).
2. **Per-gate fixed cost dominates small circuits on this machine.** [DERIVED] At n = 4 the circuit is 42 gates and takes 4.0 ms, about 94 µs per gate. Calibration measured launch overhead at 88.5 µs. Phase 10's native-Linux RTX 3050 figure was about 16 µs per gate (0.67 ms / 42). The WSL driver path is a likely cause, but that is not measured. A specialised kernel does not touch this cost.
3. **The generic kernel is about 3× off memory bandwidth at large n.** [DERIVED] At n = 22 the circuit is 204 gates on a 64 MiB state. Subtracting the transfers (2 × 64 MiB at about 3 GB/s, about 45 ms) and the per-gate fixed cost (about 18 ms) leaves about 1.65 ms per gate. A bandwidth-bound sweep (read and write 64 MiB each) at the RTX 4060 Laptop's nominal 256 GB/s is about 0.5 ms. This is the part specialised kernels can reclaim. The nominal bandwidth is a datasheet figure, not measured here; step 1 measures it.

So the probe's figures cannot be reproduced as recorded, for four reasons:
- the GPU is different (RTX 4060 Laptop in WSL, not the RTX 3050);
- the probe's ratio was taken against a single-threaded CPU that Phase 15 has replaced;
- the transfers alone (about 45 ms at n = 22) cap the end-to-end ratio against a 0.705 s CPU at about 16×;
- the per-gate overhead differs between the two machines.

Section 2 proposes what "reproducing the probe" should mean instead (Q2).

---

## 2. Step 1: re-measure the probe and take the baseline — [MENOS]

No kernel change is made before this step is measured.

1. **The probe, rebuilt in the repository.** The probe is added as benchmark-only code under `benchmarks/`, with hand-written H and CX kernels in the probe's style. It is measured beside the generic kernel on the same GPU, circuit family and timing method. It is not linked into the library. Its figure of merit is the **specialised ÷ generic ratio on the same machine**, not a CPU ratio (Q2). `docs/adr/0014` section 16.3 records that genericity cost about 4–8×. If the rebuilt probe shows a materially smaller gap, the ADR records why (for example, part of the gap was transfers or launch overhead) before any kernel is designed.
2. **A per-gate profile.** The time per gate is split into its parts (the two constant-memory copies, the launch, kernel execution, the synchronisation) at small n (4–12) and at large n (20–26). Nsight Systems is used if it works under WSL; otherwise CUDA events around each part. This tells whether the bottleneck at each size is the kernel or the host-side fixed cost.
3. **Bandwidth.** Device-to-device copy bandwidth is measured on the gate machine. It gives the bandwidth-bound floor a kernel is judged against.
4. **Baseline.** The crossover benchmark (statevector and expectation) and the dashboard's CUDA columns are run with the hygiene of `docs/adr/0019` section 2: 10 repeats, spread beside the median, power state recorded, on AC. Data goes into `docs/benchmarks/phase-16-baseline/`.

The owner then sets the gate targets from this baseline (Q6), and steps 2–4 get their own implementation plan.

## 3. Step 2: specialised 1- and 2-qubit kernels — [MENOS]

This step mirrors Phase 15 step 3 (`docs/adr/0019` section 4).

- `kernels::cuda::apply_matrix` keeps its signature and dispatches on `operands.size()`. k = 1 and k = 2 go to new kernels that insert the operand bits with two or three shifts and masks, instead of a per-bit loop. They hold the 2 or 4 amplitudes in registers and read the matrix from `__constant__` memory as now. k ≥ 3 stays on the generic kernel, which remains the oracle.
- **Bitwise identity.** Each output amplitude is computed exactly as the generic kernel computes it: starting from zero and adding `matrix[out][in] * a[in]` in increasing `in` order, with `-fmad=false` kept. The speed-up comes from indexing and memory access, never from reordering arithmetic. Tests compare each specialised kernel with the generic one on random states and random unitaries, on every operand position and order (k = 2 reversed and non-adjacent), by exact equality. `CpuCudaDifferentialTest` stays unmodified and must still pass element by element.
- **Matrix-shape kernels (diagonal, permutation) are not proposed.** A CX kernel that swaps amplitudes, or a diagonal kernel that skips the zero products, is faster but is **not** bitwise identical to the generic kernel. The generic kernel computes `0 · a + 1 · b`, which differs from `b` when `b` is −0.0, and a zero matrix entry times an infinite or NaN amplitude gives NaN, not 0. Such kernels need an owner decision to relax bitwise identity (Q3). The probe's H and CX kernels were of this kind; that alone may explain part of its gap.
- **Thread geometry and memory access** (one or several amplitude groups per thread, grid size) are chosen by measurement in step 2, not here.

## 4. Step 3: per-gate host overhead — [OPEN] (Q4)

If the step-1 profile shows host-side fixed cost dominating, two changes would address it:

- **(a) Fewer copies per gate.** Upload the matrix and operands in one copy, or pass a 1- or 2-qubit matrix (at most 16 complex values, 256 bytes) as a kernel argument instead of `__constant__` memory. This changes nothing that is observable outside the kernel.
- **(b) No synchronisation per gate.** Launch gates asynchronously and synchronise only at the end and at the monitor's boundaries. This is **not** observable-free. The Phase 11 monitor reads wall-clock time at each gate boundary, and with asynchronous launches that time no longer includes the gate's device work. One option is to synchronise only when a monitor is installed (when a time budget is set), which keeps Phase 11's semantics for budgeted jobs and changes the cost of unbudgeted ones only. That is a change to a frozen phase's behaviour and needs the owner's decision.

Proposed: (a) is in scope if step 1 shows the copies in the bottleneck. (b) needs Q4.

## 5. Step 4: recalibration and the crossover — [MENOS]

Calibration is re-run on the gate machine. The before/after device rate, launch overhead and every planner decision that changes are recorded, including the CPU/CUDA crossover (n = 16 after Phase 15). `docs/adr/0019` section 10.8 already records that the device calibration varied tenfold between runs on this machine, and that the planner picks CUDA at n = 16 although the CPU is faster there. Any change to the calibration method is proposed as an amendment with measurements, the rule Phase 15 used (Q5).

## 6. Out of scope

- Gate fusion (deferred in `docs/adr/0019` section 6; the same reasons apply on the device).
- A device-side sampler. Counts stay host-sampled from the copied-back state (`src/kernels/dense_kernels.h`).
- A parallel or reordered expectation reduction. The device reduction keeps its fixed order.
- Changes to the CPU kernels, multi-GPU, ROCm and Phase 17's API deferrals (including explicit backend selection from Python).

## 7. Risks and required measurements

| Risk | How it is caught |
|---|---|
| A specialised kernel differs from the generic one by a bit | Exact-equality kernel tests on random states and unitaries; `CpuCudaDifferentialTest` unmodified |
| FMA contraction re-enabled by a flag change | The existing `-fmad=false` setting is kept; the bitwise tests fail if it is lost |
| The kernel is fast but the end-to-end time does not move (transfers, launch) | Step-1 per-gate profile; gate measured end to end, transfers included |
| The probe's gap does not reproduce | Recorded and explained before any kernel design (section 2) |
| WSL overhead masks the kernel gain at small n | Profile reports small and large n separately |
| Device calibration noise changes planner decisions run to run | Recorded in step 4, as in Phase 15 |
| GPU tests silently skip | Gate requires the `cuda12` builds, where GPU tests run on this machine (`docs/adr/0014` section 17) |

## 8. Acceptance evidence the gate will need (from `docs/BUILD_PLAN.md`)

At minimum: the probe reproduced (or its difference explained); a measured end-to-end speed-up over the generic kernel, transfers included; bitwise identity with the CPU primitive; the new crossover recorded; `scripts/verify.sh` passing with GPU tests that execute. The numeric targets follow step 1 (Q6).

## 9. Owner decisions needed

- **Q1 — Gate machine.** Proposed: the RTX 4060 Laptop GPU in the WSL clone, through the `cuda12` presets. It is the only GPU where verification runs the GPU tests, and the machine Phase 15's figures come from. The alternative is the RTX 3050 machine where the probe was recorded, if it is still available.
- **Q2 — What "reproducing the probe" means.** Proposed: the rebuilt probe is compared with the generic kernel on the gate machine (specialised ÷ generic), not with the CPU. Reason: the probe's 57–134× was against a single-threaded CPU on a different machine (section 1.2). The test is whether the recorded 4–8× genericity cost is reproduced or explained.
- **Q3 — Bitwise identity for the specialised kernels.** Proposed: specialised kernels are general 1- and 2-qubit matrix kernels, bitwise identical to the generic kernel (section 3). Diagonal and permutation kernels, which cannot be bitwise identical in every case, are excluded unless you relax the property for them.
- **Q4 — Per-gate host overhead.** (a) fewer copies per gate: in scope if step 1 shows it in the bottleneck (proposed). (b) asynchronous launches: in scope, with synchronisation kept only when a monitor is installed; or out of scope, keeping one synchronisation per gate?
- **Q5 — Calibration.** May step 4 change the device calibration method, by an amendment with measurements, as Phase 15's step 5 could (proposed yes)?
- **Q6 — Targets.** Proposed: as in Phase 15, step 1 is implemented and measured first, and you set the numeric targets from that baseline before steps 2–4.

### Owner answers (2026-10-08)

1. **Q1:** the gate machine is the RTX 4060 Laptop GPU in the WSL clone, through the `cuda12` presets.
2. **Q2:** the specialised probe is rebuilt in the repository (benchmark-only) and re-measured, as proposed in section 2.
3. **Q3:** the specialised kernels are general 1- and 2-qubit matrix kernels, bitwise identical to the generic kernel. Diagonal and permutation kernels are excluded.
4. **Q4** (answered after the step-1 profile, section 10.3): (a) is in scope. For (b) the owner left the choice to the implementer, between one synchronisation per gate and whichever gives better results; **one synchronisation per gate is kept**. Reasons: the launch-plus-synchronisation part it could save is about 36–67 µs per gate (section 10.3), which matters only below n ≈ 16, where the CPU is faster and the planner already chooses it; and asynchronous launches would change what the frozen Phase 11 monitor observes at each gate boundary. Asynchronous launches are a recorded deferral.
5. **Q5:** step 4 may change the device calibration method, by an amendment with measurements.
6. **Q6:** the same order as Phase 15: step 1 is implemented and measured first, and the owner sets the numeric targets from that baseline before steps 2–4. The owner first chose N = 2 and M = 2 against a stored baseline. Run b then showed the generic kernel's speed moving 25–40% between runs, with both targets above the physical ceiling against run b (sections 10.4–10.5). The owner then chose same-run targets: 1.5× end to end, 1.6× for 1-qubit gates and 2× for 2-qubit gates. Step 2 then showed that bitwise-identical general kernels are limited by FP64 arithmetic, not memory, on this GPU, which puts those targets out of reach (section 10.7). The owner reset the targets to the measured figures on 2026-10-08 (section 11).

---

## 10. Step-1 results (2026-10-08)

Two runs of `docs/benchmarks/phase-16-baseline/scripts/baseline.sh` on the gate machine (RTX 4060 Laptop GPU, driver 566.14, WSL, `cuda12-release`): run a on `9a4b827`, run b on `fbbcb7e` (a data-only commit after `9a4b827`). Both were on AC at every check, Windows power scheme "Balanced", 10 repeats per point after a warm-up. Medians below. Data: `docs/benchmarks/phase-16-baseline/run-a/` and `run-b/` (`crossover.log`, `kernel.log`, their JSON, `power.log`).

### 10.1 The probe [MEASURED, `kernel.log` table (a)]

Generic ÷ probe on the crossover circuit family, both paths launching and synchronising once per gate:

| | run a | run b |
|---|---|---|
| gates only, n = 4–26 | 1.79–4.29× | 1.33–4.51× |
| gates only, n = 22–26 | 3.26–3.46× | 2.01–2.20× |
| end to end, n = 22–26 | 2.96–3.13× | 1.86–2.05× |

The recorded 4–8× genericity cost (`docs/adr/0014` section 16.3) is **not reproduced**: the measured gap is about 2–4.5×. Three causes are measured or follow from the measurements:

1. **The probe's CX is a permutation.** It swaps the two amplitudes whose control bit is 1 and leaves the other half of the state untouched. At n = 26 the probe's CX takes 7.2–7.5 ms against 14.0–14.9 ms for its H, which reads and writes every amplitude (table (b)). A bitwise-identical general 2-qubit kernel (Q3) must read and write the whole state, so this part of the probe's lead is not available to Phase 16.
2. **Per-gate fixed cost dominates below n ≈ 18.** It is common to both paths (section 10.3), which compresses the ratio there.
3. **The generic kernel's speed varies between runs; the probe's does not** (section 10.4). The ratio depends on the run.

The probe's H is at the memory-bandwidth floor at large n: 14.9 ms (run a) and 14.0 ms (run b) at n = 26, against a 13.9 ms read-and-write pass (section 10.2). The probe's final states differ from the generic kernel's by at most 1.22e-15, as expected for a kernel that computes H as (a ± b)/√2.

### 10.2 Bandwidth [MEASURED, table (c)]

Read-plus-write bandwidth is 144–155 GB/s at n = 22–26 in both runs, for a device-to-device copy and for a kernel that reads and writes every amplitude once. At n = 20 it is 303–320 GB/s, because the 16 MiB state fits in the GPU's L2 cache (32 MiB, a datasheet figure). The datasheet's 256 GB/s is not reached. One read-and-write pass over the state takes 0.90–0.93 ms at n = 22, 3.50–3.55 ms at n = 24 and 13.9 ms at n = 26: the floor for one gate.

### 10.3 One gate's cost in parts [MEASURED, table (b)]

Nsight Systems does not work under WSL on this machine. Both runs recorded no kernel activity and only part of the CUDA API trace: the importer reported errors in `nsys-run.log`, including "Data member NumTpcs was not initialized". Its output is kept as evidence and not used. The split comes from CUDA events and host timers instead, the fallback of section 2.

| Part (µs, medians, n = 4–16) | run a | run b |
|---|---|---|
| generic gate, host wall | 115–227 | 81–181 |
| the two `cudaMemcpyToSymbol` copies | 90–109 | 74–101 |
| empty launch plus synchronize | 47–64 | 36–67 |

Below n ≈ 18, a gate's cost is host-side fixed cost, and the two constant-memory copies are the larger part of it. At n = 22–26 the gate itself dominates:

| Generic gate (ms), run a / run b | n = 22 | n = 24 | n = 26 |
|---|---|---|---|
| k = 1 | 2.71 / 1.82 | 10.21 / 6.58 | 38.92 / 29.15 |
| k = 2 | 3.82 / 2.37 | 13.75 / 9.30 | 53.98 / 41.69 |
| floor (section 10.2) | 0.93 | 3.50 | 13.9 |

[DERIVED] At n = 26 the generic kernel is 2.1–2.8× off the floor for k = 1 and 3.0–3.9× for k = 2. Removing the copies (Q4 (a)) addresses the small-n cost. Specialised kernels address the large-n cost.

### 10.4 Run-to-run variation [MEASURED]

Between runs a and b, the generic kernel's large-n gate times moved by 25–40% (table above). The crossover benchmark's CUDA times at n = 22 and 24 moved from 0.637 s and 1.985 s to 0.405 s and 1.642 s. Over the same runs, the probe's H, the copy bandwidth and the touch kernel moved by under 7%. The cause is not measured. One plausible explanation: the generic kernel is limited by instruction issue rather than by memory, so it follows the GPU clock, which is not controlled under WSL. A target set against a stored baseline would measure this variation as much as the kernels.

### 10.5 What the targets can reach [DERIVED]

Assume every gate runs at the floor of section 10.2 and the copy back runs at the measured 3.1 GB/s. Against run b, the faster generic run, the end-to-end speed-up over the generic kernel in the kernel benchmark is then at most about 1.9× at each of n = 22, 24 and 26. The per-gate speed-up at n = 22–26 is at most about 1.9–2.1× for k = 1 and 2.6–3.0× for k = 2. So against run b, the first proposal (N = 2 end to end and M = 2 per gate, against a stored baseline) is above the ceiling end to end at every size, and for k = 1 gates.

**Correction (2026-10-08, after step 2).** The ceilings above assume a gate is bound by memory bandwidth. Step 2 showed that a bitwise-identical general matrix kernel is bound by FP64 arithmetic on this GPU instead (section 10.7). The real ceilings are lower, and the 2-qubit ceiling is about 1.0–1.1×.

### 10.6 CPU against CUDA [MEASURED, `crossover.log`]

Statevector output: CUDA is first faster at n = 16 in both runs (1.45× in run a, 1.05× in run b, where n = 18 is 0.99×). At n = 20–24 it is 1.28–1.86× faster. Expectation output: CUDA is first faster at n = 18 in both runs (1.60×, 1.22×), and 0.99–2.01× at n = 20–24. Calibration measured launch overhead at 73 µs in run a and 123 µs in run b.


### 10.7 Step 2: the specialised kernels are FP64-bound [MEASURED, `docs/benchmarks/phase-16-step2/`]

The specialised kernels (`f495da6`) are bitwise identical to the generic kernel at every size measured (`kernel.log`, the `bitwise` column). The same-run ratios are taken from one run of `menos_cuda_kernel_benchmark` (`e10aa9c`, on AC):

| Same run, generic ÷ specialised | n = 4–14 | n = 16–18 | n = 22 | n = 24 | n = 26 |
|---|---|---|---|---|---|
| end to end | 1.63–2.06× | 1.30–1.41× | 1.22× | 1.22× | 1.25× |
| 1-qubit gate | 1.89–3.12× | 1.74× (n = 16) | 1.48× | 1.50× | 1.48× |
| 2-qubit gate | 1.73–2.32× | 1.34× (n = 16) | 1.06× | 1.02× | 1.02× |

Removing the per-gate copies (Q4 (a)) roughly halves a small gate's cost. At large n the 2-qubit kernel is no faster than the generic one. The reason is measured:

1. **The arithmetic is fixed by bitwise identity.** Under `-fmad=false`, the 1-qubit kernel compiles to 16 DMUL and 16 DADD per amplitude pair, and the 2-qubit kernel to 64 DMUL and 64 DADD per group of four, with no DFMA (`sass_counts.txt`). The generic kernel performs the same products and sums for the same k, because bitwise identity requires it.
2. **This GPU's FP64 rate is low.** A scratch kernel of independent DMUL/DADD chains (`fp64peak.cu.txt`) measured 44 G FP64 instructions/s at one moment and 86 G/s at another (`fp64peak.log`): 48 FP64 lanes at the clock of the moment. FP64 runs at 1/64 of the FP32 rate on this consumer GPU.
3. **The specialised kernels run at the FP64 peak.** `fp64_fraction.cu.txt` alternates the peak kernel with verbatim copies of the two specialised kernels at n = 26, so each pair shares one GPU clock. Both kernels run at 0.99–1.01 of the peak in every repetition (`fp64_fraction.log`). At full clock, the 1-qubit gate takes 12.5 ms against 9.35 ms for a memory pass of the same state, so it is arithmetic-bound even then.

So a 2-qubit gate cannot be made materially faster than the generic kernel without performing fewer FP64 operations, which Q3 rules out. The 1-qubit gate gains only from cheaper indexing. The probe was fast because its H does 4 FP64 operations per pair rather than 32, and its CX does none.

The same measurements show that **the GPU's clock state changes by about 2× between runs.** In the runs above, the FP64 peak moved from 44 to 86 G/s and the memory pass from 154 to 230 GB/s. This explains the step-1 run-to-run variation (section 10.4) and supports the same-run ratios.

## 11. Phase 16 acceptance gate (owner, 2026-10-08)

The gate is written in `docs/BUILD_PLAN.md`, Phase 16. Its speed-ups are **same-run** ratios. The shipped `kernels::cuda::apply_matrix` is measured against `kernels::cuda::apply_matrix_generic` (the unchanged generic kernel, kept as the oracle) in the same run of `menos_cuda_kernel_benchmark`, on the same circuit, so both see the same GPU clock (section 10.4).

The owner first set 1.5× end to end, 1.6× per 1-qubit gate and 2× per 2-qubit gate at n = 22–26. After section 10.7, the owner kept Q3 and reset the targets on 2026-10-08 to the measured figures with margin:
- end to end at least **1.4×** for small circuits (n = 4–14) and at least **1.15×** at n = 22–26;
- per gate at n = 22–26, at least **1.3×** for 1-qubit gates and at least **1.0×** (no regression) for 2-qubit gates. Because step 2 measured 1.02–1.06× for 2-qubit gates, within run-to-run noise of 1.0, the owner then set the 2-qubit threshold to **0.95×** (2026-10-08).

Bitwise-exact kernels for diagonal and permutation gates, which could skip zero products while reproducing IEEE results exactly, are a recorded deferral. They need an amendment to Q3 and their own ADR.

## 12. Step 4: recalibration and the crossover [MEASURED, `docs/benchmarks/phase-16-gate/`]

The calibration method is unchanged (Q5 not invoked). Figures are from the crossover benchmark's calibration line: step-1 baseline runs a and b against gate runs a and b (`b84f273`).

| Calibration | baseline a | baseline b | gate a | gate b |
|---|---|---|---|---|
| device rate (work units/s) | 6.15e9 | 4.68e9 | 7.49e9 | 4.64e9 |
| launch overhead (µs) | 73 | 123 | 30 | 104 |
| host rate (work units/s) | 1.01e9 | 7.83e8 | 1.08e9 | 4.42e8 |

`measure_kernel_launch_seconds` times `apply_matrix` on a 1-qubit identity, so it now measures the copy-free path. Gate run a, at full clock, measured 30 µs against 73 µs at baseline. Gate run b ran with the GPU and the CPU throttled (its generic kernel times are about twice run a's), and its figures overlap the baseline's. The planner's launch term now also applies, uncorrected, to k ≥ 3 gates, which still make the two constant copies. These gates are rare (CCX and user unitaries of 3–5 qubits), and the effect was not measured separately.

**Forced CPU/CUDA crossover** (`crossover.log`, CUDA ÷ CPU end to end, CPU on all threads):

| | n = 14 | n = 16 | n = 18 | n = 20 | n = 22 | n = 24 |
|---|---|---|---|---|---|---|
| statevector, baseline a / b | 0.44 / 0.23 | 1.45 / 1.05 | 1.63 / 0.99 | 1.78 / 1.39 | 1.28 / 1.79 | 1.54 / 1.86 |
| statevector, gate a / b | 0.96 / 0.81 | 2.34 / 2.59 | 2.56 / 2.85 | 2.22 / 2.56 | 1.42 / 1.71 | 2.78 / 1.51 |
| expectation, baseline a / b | 0.25 / 0.20 | 0.73 / 0.60 | 1.60 / 1.22 | 0.99 / 1.08 | 1.21 / 1.17 | 1.63 / 2.01 |
| expectation, gate a / b | 0.66 / 0.63 | 1.59 / 1.22 | 2.79 / 1.58 | 2.68 / 2.18 | 2.49 / 2.42 | 2.04 / 1.86 |

With statevector output, CUDA is first faster at n = 16 before and after, but by 2.3–2.6× now against 1.05–1.45×. With expectation output, CUDA's first win moved from n = 18 to n = 16. These are comparisons across runs, so the machine-state variation of section 10.7 applies to them; the same-run ratios of section 13 do not have that problem.

**Planner decision** (`planner_choice.sh`, `menos::preview` on the crossover circuit family, statevector, `cuda12-release`; the generic-only build `9680b6b` and the current build alternated three times, each a fresh calibration, `planner_choice.log`):

| n | 10–12 | 14 | 16 | 18–26 |
|---|---|---|---|---|
| before (`9680b6b`) | CPU | CPU, CPU, CPU | CPU, CUDA, CUDA | CUDA |
| now (`2737b57`) | CPU | CUDA, CUDA, CPU | CUDA, CUDA, CUDA | CUDA |

The CPU→CUDA switch moved from n = 16 to n = 14. At n = 16, choosing CUDA is now correct by a wide margin (2.3–2.6× above). At n = 14 the planner chooses CUDA in two of three calibrations, while CUDA measured 0.81–0.96× of the CPU there, so that choice is wrong by 4–19%. The cause is the device calibration's run-to-run variation (the launch figure above varies 3.4×), which `docs/adr/0019` section 10.8 already recorded at n = 16. It is not caused by the kernels. No calibration-method change is proposed: the error is small, sits at the boundary, and a method change needs its own measurements (Q5). No planner golden file changed.

## 13. Gate evidence (2026-10-08)

Gate runs a and b: `docs/benchmarks/phase-16-gate/scripts/gate.sh` on `b84f273`, on AC at every check (`run-a/power.log`, `run-b/power.log`). `gate_criteria.py` over both runs exits 1, and **criterion 1 fails at one point**. Every other numeric criterion passes in both runs.

| Criterion | Target | Run a | Run b | Verdict |
|---|---|---|---|---|
| 1, end to end, n = 4–14 | ≥ 1.4× | 1.58–2.20× | **1.39× at n = 8**; 1.40–2.30× elsewhere | **FAIL (n = 8, run b)** |
| 1, end to end, n = 22–26 | ≥ 1.15× | 1.17–1.27× | 1.25–1.28× | PASS |
| 2, 1-qubit gate, n = 22–26 | ≥ 1.3× | 1.40–1.42× | 1.41–1.52× | PASS |
| 2, 2-qubit gate, n = 22–26 | ≥ 0.95× | 1.00–1.04× | 1.00–1.05× | PASS |
| 4, no regression, n = 4–18 | specialised ≤ 1.10 × generic | 0.46–0.67 | 0.43–0.72 | PASS |

The failing point: in run b at n = 8, generic took 13.23 ms (median) and specialised took 9.50 ms (median; interquartile range 7.53–11.98 ms), giving 1.39× against 1.4×. Run a measured 1.58× at the same point. In run b the GPU and CPU were throttled: the generic kernel's times were about twice run a's, and calibration measured a CPU rate of 4.42e8 work units/s against 1.08e9 (section 12). The shortfall (0.01×) is far inside that point's own spread. Under the gate's rule (both runs must meet every point), criterion 1 is not met. **This needs an owner decision**; no criterion has been changed.

Remaining criteria:

3. **Bitwise identity.** `CudaSpecialisedKernelsTest` (5 tests) executes and passes in `cuda12-dev` and `cuda12-release`. Deliberate FMA and reordered-sum mutations make it fail (section 10.7 records the kernels). The kernel benchmark's `bitwise` column is "yes" at every n in both gate runs. `CpuCudaDifferentialTest` is unmodified and passes in both cuda12 stages.
5. **The probe explained.** Sections 10.1 and 10.7.
6. **Recalibration and crossover recorded.** Section 12.
7. **`scripts/verify.sh` exits 0 on `da0ad8b`** (WSL clone, `UV_PYTHON_PREFERENCE=only-managed`):
   - dev 619/619 and release 619/619;
   - pytest 236 passed, 1 deselected;
   - cuda-dev and cuda-release 657/657, with the GPU tests skipping (CUDA 13 above the WSL driver's CUDA 12.7, as in every phase since 12);
   - cuda12-dev 657/657 (2 skipped) and cuda12-release 657/657 (1 skipped), with GPU tests executing.

   Every skip in the cuda12 stages is a skip Phase 15 already had (`CudaExecutionMonitorTest.TenAbandonsOfAnOversubscribingTotalNeverExhaustTheDevice`, and in dev `WallTimeReportTest.TwentyFourQubitsTwoHundredOperations`). The documentation site was not built (Node is not installed).
8. **Documentation.** This ADR, `docs/BUILD_PLAN.md`, `docs/engines/cuda.md`, `docs/reference/limitations.md` and `docs/reference/changelog.md`. `apply_matrix_generic` has a production caller (`apply_matrix`, k ≥ 3), tests, and the benchmark. The README's statements (bitwise-identical CUDA, CPU/CUDA selection by calibration) are unchanged and still true.

### 13.1 Owner ruling and acceptance (2026-10-09)

The owner accepted the Phase 16 gate on 2026-10-09. They ruled that the single miss above (criterion 1, n = 8, gate run b, 1.39× against 1.4×) does not count against the gate, on the grounds recorded with it: the shortfall is 0.01× and inside that point's interquartile range, run b ran with the GPU throttled, and run a measured 1.58× at the same point. The threshold itself is unchanged. Every other criterion passes in both runs. Phase 16 is COMPLETE.
