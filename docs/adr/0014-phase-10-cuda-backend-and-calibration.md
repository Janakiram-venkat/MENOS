# ADR 0014: Phase 10 — CUDA as the first GPU backend, hardware calibration, and `time_budget`

## Status

**COMPLETE -- Phase 10 acceptance gate passed, confirmed by the owner on
2026-09-30.**

Section 16 records what was deliberately NOT done and why, so the deferrals are
decisions on record rather than gaps someone rediscovers later.

**Implemented.** The owner selected CUDA as the first GPU backend (Metal
deferred). Calibration and `time_budget` (sections 3-6) came first, while no
compiler existed. The owner then installed a toolkit and the CUDA dense
backend, the primitive API, the differential tests and the crossover benchmark
followed (sections 8-14).

Section 1 is preserved as the record of the machine BEFORE the toolkit was
installed, because it is what justified stopping rather than guessing. Section 2
has been superseded by section 8 onward and is kept only for that history.

Three kinds of statement are kept visibly separate:

- **[MEASURED]** — a number produced by a command run on this machine.
- **[MENOS]** — a design decision made here.
- **[BLOCKED]** — specified work that cannot proceed, with the reason.

---

## 1. The toolchain boundary, measured (HISTORICAL -- before the toolkit was installed)

The owner's instruction was to establish the existing CUDA environment
exhaustively before assuming anything about installation. The result:

### Present [MEASURED]

| Component | Detail |
|---|---|
| GPU | NVIDIA GeForce RTX 3050 Laptop GPU (GA107M) |
| Compute capability | 8.6 (`nvidia-smi --query-gpu=compute_cap`) |
| VRAM | 4096 MiB |
| Driver | 595.91.07, advertising CUDA 13.2 |
| Driver API library | `/usr/lib/x86_64-linux-gnu/libcuda.so` → `libcuda.so.595.91.07` |
| Kernel modules | `nvidia`, `nvidia_uvm`, `nvidia_drm`, `nvidia_modeset` loaded |
| Device nodes | `/dev/nvidia0`, `/dev/nvidiactl`, `/dev/nvidia-uvm` |
| Host compilers | GCC 15.2.0, clang++ 21.1.8 |

### Absent [MEASURED]

| Missing | Evidence |
|---|---|
| `nvcc` | `command -v nvcc` → not found; bounded `find` over `/usr /opt /snap /home` → nothing |
| CUDA Toolkit | no `/usr/local/cuda*`, `/usr/lib/cuda`, `/usr/lib/nvidia-cuda-toolkit` |
| NVIDIA CUDA headers | no `cuda_runtime.h`; no NVIDIA `cuda.h` |
| usable `libcudart` | only inside ollama's private bundle, `/usr/local/lib/ollama/cuda_v{12,13}/`, bare `.so` with no headers |
| `libdevice` bitcode | absent (required by clang's CUDA path) |
| NVRTC | absent |

The `cuda.h` files that do exist (`/usr/include/linux/cuda.h`,
`/usr/src/linux-headers-*/include/linux/cuda.h`) are the Linux ADB/PowerMac
CUDA controller header, unrelated to NVIDIA CUDA. Recorded explicitly so a
later reader does not mistake them for a usable toolkit.

clang++ was tested as a substitute rather than reasoned about:

```
$ clang++ -x cuda /tmp/t.cu -o /tmp/t.out
clang++: error: cannot find libdevice for sm_52; provide path to different CUDA
         installation via '--cuda-path', or pass '-nocudalib'
clang++: error: cannot find CUDA installation; provide its path via '--cuda-path'
```

**Conclusion.** The driver stack is complete; only the compiler and toolkit are
missing. Device kernels cannot be produced by any means available here.

### What was rejected, and why [MENOS]

- **Hand-declaring the CUDA driver API against absent headers**, or
  **hand-writing PTX**. Both would mean inventing API behavior and binary
  contracts from memory, which prime directive 6 forbids outright. Neither is
  proposed.
- **Vendoring a `libcudart` from ollama's bundle.** Libraries without headers
  give no checked interface, and the version is not ours to pin.

---

## 2. The CUDA backend [SUPERSEDED by sections 8-14 -- historical]

At the time this section was written it was **not implemented**, for the reason
in section 1. Retained so the record shows what was deferred and why:

- `src/backends/cuda/`, guarded by `MENOS_ENABLE_CUDA`, defaulting **OFF** so
  a machine without CUDA keeps passing the existing gate unchanged.
- The primitive kernel API, to be extracted from the dense engine's CPU code
  **only once the CUDA backend exists**. Extracting it now would produce an
  interface with one implementation, which prime directive 3 forbids: "the
  kernel primitive API arrives with the second hardware backend."
- CPU/GPU differential tests, tolerance derived in `common/tolerance.h`.
- A crossover benchmark reporting end-to-end runtime including transfers as
  the headline figure, kernel-only separately.

### A risk to record before the work, not after [MEASURED]

This GPU has **4 GiB** of VRAM; the CPU memory budget used throughout this
project is **7.75 GiB**. A dense complex128 statevector is `16 · 2^n` bytes, so
the GPU caps out near **n = 27** before working space, while the CPU path
reaches **n = 28-29**. The GPU is therefore memory-limited *below* the CPU, and
any crossover must appear below ~27 qubits or not at all. The Phase 10 gate
says: "If the GPU backend is not faster anywhere useful, do not ship it." That
outcome is a real possibility on this hardware and will be reported as measured.

---

## 3. Calibration: what is measured and how

`plan::calibrate_hardware()` produces two figures, cached for the process
lifetime by `plan::calibrated_hardware_profile()`.

### 3.1 Methodology [MENOS]

Both microbenchmarks are **deadline-bounded** (~15 ms each) rather than
fixed-iteration, with a minimum of two repetitions and a **discarded warm
pass**. Fixed iteration counts were rejected because a Debug+sanitizer build is
roughly an order of magnitude slower than Release, so any count tuned for one
is wrong for the other.

- **Bandwidth** — `memcpy` between two 32 MiB buffers (64 MiB footprint,
  above the 16 MiB L3 on this machine), counting `2 × 32 MiB` of traffic per
  repetition. `memcpy` rather than a hand-written element loop for two reasons:
  it is the standard streaming-bandwidth measurement, and a sanitizer
  instruments it as one interceptor call rather than per element, so the figure
  is not swamped by instrumentation unrelated to the memory system.

- **Throughput** — a real 13-qubit, 2-layer H+CX circuit through the real
  `DenseEngine`, dividing the **cost model's own** estimated work units for
  that circuit by measured wall time. This calibrates the cost model's unit
  against this machine. No constant relating flops to work units is invented
  anywhere, which is the point.

### 3.2 Two defects found and fixed during development [MEASURED]

Recorded because both produced a plausible-looking wrong number, which is the
failure mode prime directive 7 exists to prevent.

1. **Dead-store elimination silently emptied the benchmark.** The timed
   `memcpy`s write a buffer nothing else observes, so at `-O3` GCC deleted
   every one of them. The warm pass then reported **558691 copies in 15 ms —
   2.5 PB/s**, i.e. an empty loop. Because the warm-up thereby warmed nothing,
   the measured pass ran cold and reported **half** the machine's real rate.
   Fixed by reading the destination through a `const volatile char*` after each
   copy, making each copy observable.
   `CalibrateHardwareTest.BandwidthIsPhysicallyPlausible` is the regression
   test and bounds the figure at 1 TB/s.

2. **A single warm-up copy was not enough.** With one warm-up, a Release build
   spent longer than the whole deadline inside its first timed copy, exited at
   one repetition, and reported ~6× below steady state. Fixed by running the
   entire deadline loop once and discarding it.

After both fixes the figure is a property of the machine rather than of the
build, which is the correctness criterion:

| Build | bandwidth (B/s) | work units/s | calibrate() cost |
|---|---|---|---|
| dev (Debug + ASan/UBSan) | 2.48–2.51 × 10¹⁰ | 1.334 × 10⁷ | 0.32 s |
| release (`-O3`) | 2.71–2.85 × 10¹⁰ | 3.8 × 10⁸ | 0.09 s |
| standalone reference `-O3` | 2.75–2.84 × 10¹⁰ | — | — |

Bandwidth now agrees across builds to ~12% and matches an independent
standalone `memcpy` benchmark. Throughput correctly differs by ~29× between
builds, because it is measuring each build's actual speed — which is the right
answer for a figure used to decide whether *this* build can finish in time.

### 3.3 A stated limitation [MENOS]

A 13-qubit statevector is 128 KiB and therefore **cache-resident**. The
throughput figure is consequently an **optimistic** upper bound: a 28-qubit run
streams 4 GiB per gate layer and cannot reach it.

This is deliberate. A DRAM-sized calibration circuit was rejected **on
measurement, not assumption**: the rate varies by under 1% across 12, 13, 14
and 16 qubits, so a larger circuit buys no accuracy while costing seconds per
process under a sanitizer. The optimism is instead corrected by the bandwidth
floor in section 4.

---

## 4. How `time_budget_seconds` is enforced

### 4.1 The estimate [MENOS]

For each feasible candidate the planner computes

```
estimated_seconds = max( estimated_work_units / work_units_per_second ,
                         estimated_peak_bytes / memory_bandwidth_bytes_per_second )
```

**What each term bounds, stated explicitly** — CLAUDE.md requires this, because
validating a metric's arithmetic is not validating the metric:

- The **work term** converts the cost model's own unit through the calibrated
  rate. Calibrated cache-resident, so optimistic at large `n` (section 3.3).

- The **traffic term** is a physical floor that corrects it. A candidate that
  materializes `estimated_peak_bytes` must move at least that many bytes across
  the memory bus, so it cannot finish sooner than those bytes take at the
  machine's measured streaming rate. It bounds **the time to write the peak
  working set once**. It is deliberately *not* a model of a run's total traffic
   — a dense sweep re-reads its state every gate layer and moves far more — so
  it is a true lower bound rather than an estimate, and it is the weakest claim
  that holds unconditionally for every engine. An engine-specific traffic model
  would need a measurement this phase does not have.

This is what makes `memory_bandwidth_bytes_per_second` genuinely *consumed by
the cost model*, as Phase 10 requires, rather than merely exposed.

### 4.2 Semantics [MENOS]

A **constraint, never a hint.** A candidate whose `estimated_seconds` exceeds
the budget is marked infeasible with a reason naming the budget, exactly as an
over-budget memory estimate is. If no candidate survives, planning throws
`ResourceExceeded`. It is never downgraded to "run the cheapest one anyway".

It bounds the **estimate**, not the execution: nothing is interrupted mid-run,
because this phase has no monitoring to cancel with (that is Phase 11). Stated
so the guarantee is not overread.

> **AMENDED by Phase 11 (`docs/adr/0015`): this limitation is lifted.** From
> Phase 11 the budget also bounds *execution*: a run that overruns it is
> abandoned at a safe boundary and replanned or refused. It bounds **one
> execution attempt**, not the total wall time of a job that replans --
> `docs/adr/0015` section 2.3. The paragraph above is kept as the record of what
> Phase 10 guaranteed.

### 4.3 An uncalibrated profile refuses the budget [MENOS]

`detect_hardware_profile()` leaves both figures at zero. A job carrying a time
budget against such a profile throws `InternalError` rather than passing.
Dividing by zero would otherwise make every candidate look instantaneous and
admit every budget — a plausible wrong result, which prime directive 7 forbids.
Without a budget an uncalibrated profile stays perfectly usable, so every
pre-Phase-10 caller is unaffected.

---

## 5. `explain()` determinism [MENOS]

`estimated_seconds` is a *measured* quantity and differs between machines.
`Plan::explain()` promises a deterministic, byte-identical report, and
CLAUDE.md's determinism invariant covers the plan.

So the seconds columns are printed **only when the plan carries a time budget**
— that is, only when the caller asked for a time-constrained plan, where a
machine-dependent answer is precisely what was requested. Without a budget the
report is byte-identical to a pre-Phase-10 one, which the unchanged CLI golden
tests demonstrate.

`Plan::time_budget_seconds` exists to carry that distinction, and explain()
reports the budget itself on a `time_budget` line when set.

---

## 6. What is tested

- `tests/plan/hardware_profile_test.cpp` — both figures positive; `detect_`
  leaves both at zero; the cache returns the same object; bandwidth is
  physically plausible (the dead-store regression); repeated calibration is
  reproducible.
- `tests/plan/planner_test.cpp` — `estimated_seconds` equals the documented
  `max(...)`; the traffic floor dominates when work is free; a generous budget
  changes nothing; an impossible budget throws `ResourceExceeded` with the
  budget named; the chosen candidate always respects the budget; an
  uncalibrated profile refuses a budget but works fine without one.

Planner tests use a synthetic unit-rate `HardwareProfile` rather than a
calibrated one, so the arithmetic is pinned exactly and the tests mean the same
thing on every machine.

---

---

# Part II -- the CUDA backend, after the toolkit was installed

## 8. Which toolkit, and why it is not a free choice [MEASURED]

The machine now carries **two** CUDA toolkits, and they are not interchangeable.

| | Ubuntu `nvidia-cuda-toolkit` | NVIDIA `cuda-toolkit-13-4` |
|---|---|---|
| Path | `/usr/bin/nvcc` (a 112-byte wrapper) | `/usr/local/cuda` -> `/usr/local/cuda-13.4` |
| Version | 12.4.131 | **13.4.92** |
| Max host GCC | **13** (`host_config.h`: `#if __GNUC__ > 13`) | **16** (`#if __GNUC__ > 16`) |
| `sm_86` | yes | yes |

The default host compiler here is **GCC 15.2.0**, so CUDA 12.4 rejects it
outright and would force a `-ccbin g++-13` split with a mixed-ABI boundary
between the `.cu` objects and the rest of the project. CUDA 13.4 accepts GCC
15.2 directly.

**Decision: `/usr/local/cuda` (13.4.92) with the default host compiler,
`-arch=sm_86` stated explicitly and never left to a default.**

Driver 595.91.07 advertises CUDA 13.2 while the toolkit is 13.4. That works by
CUDA minor-version compatibility, and it was **verified by running a kernel**,
not assumed.

### Two build traps, both of which fail silently [MEASURED]

Both were hit in practice, and both produce a build that compiles, links and
runs while being wrong. They are why the presets pin two variables instead of
none:

1. **`enable_language(CUDA)` picks the wrong compiler.** `/usr/bin` precedes
   `/usr/local/cuda/bin` on PATH, so CMake selected `nvcc 12.4` with host
   compiler GCC 13.4. Fixed by pinning `CMAKE_CUDA_COMPILER`.

2. **`find_package(CUDAToolkit)` picks the wrong runtime.** Independently of
   the compiler, it resolved `CUDA_cudart_LIBRARY` to
   `/usr/lib/x86_64-linux-gnu/libcudart.so` (12.4) while the include path was
   `/usr/local/cuda/.../include` (13.4). Headers from one major version and a
   runtime from another link without complaint and then **misread struct
   fields**: `cudaDeviceProp::multiProcessorCount` came back as **1** on a GPU
   with **16** SMs. Fixed by pinning `CUDAToolkit_ROOT=/usr/local/cuda`;
   confirmed by `ldd` showing `libcudart.so.13` and the SM count reading 16.

   Recorded prominently because the symptom was a single plausible-looking
   wrong number in a report, which is exactly the failure mode prime directive
   7 exists to prevent.

### Warning flags on `.cu` -- a documented deviation [MEASURED]

Established by compiling with one flag at a time. Applied via `-Xcompiler`:
`-Wall -Wextra -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor
-Woverloaded-virtual -Werror`.

Two of CLAUDE.md's flags are dropped for `.cu` only, neither because of MENOS
code:

- `-Wpedantic` fires on nvcc's own generated intermediate (its `# line`
  directives are a GCC extension).
- `-Wold-style-cast` fires inside CUDA's `channel_descriptor.h`.

`-Wconversion` is deliberately **kept**: it fires on real index arithmetic in
our kernels, which is where it earns its place. CUDA headers are included with
`-isystem`, the same treatment FetchContent'd third-party code already gets.

## 9. The primitive API

`src/kernels/dense_kernels.h`, the first occupant of the `kernels` layer the
architecture already named. It declares **exactly three** operations, which are
the complete set both backends call:

1. `initialize(state, global_phase)`
2. `apply_matrix(state, matrix, operands)` -- generic over operand count
3. `expectation_of_term(state, x_mask, z_mask)`

It is a **namespace-level contract, not a virtual interface**: CLAUDE.md forbids
virtual dispatch per gate in a hot loop, so the backend is chosen once at engine
granularity and every gate afterwards is a direct call. The header is also the
single place the little-endian bit order, operand-order and `Y = i*X*Z`
conventions are stated for both implementations.

The CPU bodies were **moved, not rewritten**, so the existing oracle tests keep
testing the same code.

**Counts sampling is deliberately absent.** It has one real implementation, and
adding it would be the speculative abstraction prime directive 2 forbids. See
section 11.

### The CUDA header boundary is strict

`<cuda_runtime.h>` and `<cuComplex.h>` are included **only** under
`src/kernels/cuda/`. Nothing in `engines/`, `plan/`, `runtime/` or `api/` sees a
CUDA type; they speak in menos types (`DeviceState`, `DeviceBuffer`,
`TransferRates`) and CUDA errors are translated to menos typed exceptions at the
boundary by `ensure_cuda`.

The transfer-rate and kernel-launch microbenchmarks live in
`kernels/cuda/device_benchmarks.h` for exactly this reason -- they are pure
device measurements and had no business in `plan/`. The one calibration figure
that cannot move down is the marginal device work rate, because it times a real
circuit through `CudaDenseEngine`, which is in the engines layer above; it stays
in `plan/cuda_calibration.cpp` and reuses the same timing policy.

## 10. Kernel design

Generic k-operand only. No specialised 1-qubit/2-qubit fast paths: CLAUDE.md
forbids optimisation without a benchmark showing the bottleneck, and the
crossover benchmark is what would justify them in a later change.

`kMaxOperands` is `static_assert`ed equal to `kMaxUnitaryQubits` (5), the IR's
own ceiling, so the device kernel can never reject a circuit the IR accepts.

Gate matrices and operand lists go to `__constant__` memory rather than a
per-gate `cudaMalloc`. Measured reason: a per-gate allocation costs single-digit
microseconds, negligible against a large state but dominant on a small one,
which would move the crossover right for no reason. The precondition -- no
concurrent `apply_matrix` from two host threads -- is stated in the source;
MENOS has no threaded execution yet.

### `-fmad=false`, and why it is a correctness setting [MEASURED]

nvcc fuses `a*b+c` into FMA by default, which changes rounding. With it on, CPU
and CUDA statevectors differed by up to **3.55e-15**. With it off they are
**bitwise identical** (worst difference exactly **0** across 8 qubit counts x 5
operand counts with random matrices and random operand orderings), because the
device `apply_matrix` sums its `2^k` products in the same order as the CPU.

That matters beyond tidiness: counts are sampled host-side from the returned
state, so bitwise identity is what makes CPU and CUDA counts identical **by
construction** for a given seed, rather than identical-in-practice. This is the
opposite of `-ffast-math` (which CLAUDE.md forbids): it removes a fast-math-style
transform nvcc applies by default.

## 11. What runs where

| | Device | Host |
|---|---|---|
| `initialize` | yes | |
| `apply_matrix` (all gates) | yes | |
| expectation reduction | yes | |
| counts sampling | | yes |

Gate evolution -- the dominant `O(2^n x gates)` cost -- runs entirely on the GPU.
The expectation reduction also runs on device and returns one complex per term,
so an expectation-only job never pays the `16 * 2^n` transfer back.

Counts are sampled on the host from the copied-back state using the existing
`engines::dense` sampler, unchanged. A device-side sampler would not reproduce
the CPU sampler's draw sequence, so counts would differ between backends for the
same seed, conflicting with CLAUDE.md's determinism invariant. Owner-confirmed.

No transfer of the initial state is needed at all: `initialize` runs on device
over a zeroed buffer, so only gate matrices go up.

## 12. Cost model and backend selection

### Two-level selection (owner-decided)

The existing winner rule is **untouched**: cheapest `estimated_work_units`, ties
to the earlier-enumerated candidate, chooses the **algorithm**. Only then is the
**backend** chosen, by calibrated wall-clock seconds, with **ties going to CPU**.

This matters because CPU-dense and CUDA-dense run the same algorithm and
therefore have *identical* work units -- under the old rule a CUDA candidate
could never win. `Strategy` gains no new value; `available_engines()` (the
algorithm list) is unchanged, so every existing CPU plan is byte-identical, and
a new `execution_engines()` maps `(Engine, Backend)` to an implementation for
the runtime.

Backend resolution runs **before** the time-budget check, deliberately: a job
whose budget only the GPU could meet must not be rejected on the CPU estimate.

### The seconds formula

```
CPU:   max(work_units / work_units_per_second,
           peak_bytes / memory_bandwidth)          # unchanged from Part I

CUDA:  kernel_launches * kernel_launch_seconds
     + h2d_bytes / h2d_bandwidth
     + work_units / device_work_units_per_second
     + d2h_bytes / d2h_bandwidth
```

Every term is separately measured. **The launch term is what makes the model
honest**: without it the GPU would look uniformly faster and the planner would
choose it for small circuits where it actually loses by 100x. A test
(`LaunchOverheadAloneCanMakeTheDeviceLose`) pins that behaviour.

**What each term bounds**, per CLAUDE.md's rule that validating a metric's
arithmetic is not validating the metric: `kernel_launches` bounds the fixed cost
that is paid regardless of problem size; the transfer terms bound bytes that
must physically cross PCIe; the work term bounds GPU-resident arithmetic at a
rate calibrated at 20 qubits, where the device is saturated rather than
launch-bound.

### Device memory is a separate budget

`CostEstimate::DeviceCost` carries `device_bytes`, `host_to_device_bytes`,
`device_to_host_bytes` and `kernel_launches`, all zero for CPU engines, so the
formula degenerates exactly to the CPU one for them.

The device budget comes from `cudaMemGetInfo`, **never the nameplate**: this GPU
is sold as 4096 MiB and reports **3770 MiB total / 3681 MiB free**. Budgeting
against the datasheet would over-commit by 326 MiB.

`16 * 2^n <= 3681 MiB` caps CUDA at **n = 27**, while the CPU path reaches n =
28-29 -- **the GPU is memory-limited below the CPU**. Above that CUDA is simply
infeasible and the CPU wins by default.

The budget is enforced in the planner against the profile (as the host budget
already is in `cost::estimate_direct`), and again in `DeviceBuffer` against the
physical card before any `cudaMalloc`. A test caught the first of those being
missing.

## 13. Calibration [MEASURED]

Measured on the RTX 3050 Laptop GPU, Release build:

| figure | value |
|---|---|
| host work units/s | 3.43e8 |
| device work units/s | 3.87e9 |
| host-to-device | 8.2e9 B/s |
| device-to-host | 7.3e9 B/s |
| kernel launch overhead | **1.28e-5 s** |
| device memory | 3770 MiB total / 3681 MiB free |

### AddressSanitizer silently hides the GPU [MEASURED]

Under the `dev` preset's AddressSanitizer, `cudaGetDeviceCount` **fails**: ASan's
shadow gap collides with the CUDA driver's virtual address reservation. The
backend then correctly reports "no device", every CUDA test skips itself, and
the suite passes green **while never touching the GPU**.

This is recorded because it is precisely the false-green the Phase 10 gate warns
against, and it was observed before being reasoned about: 466 tests "passed"
with CUDA compiled in and not one of them ran a kernel. Fixed by setting
`ASAN_OPTIONS=protect_shadow_gap=0` in the `cuda-dev` test preset, after which
the device is visible and the CUDA tests run.

## 14. Measured results

### CPU/CUDA agreement

Statevector and counts are required to be **bitwise identical**, not merely
close (see section 10); expectation is compared within the existing
`common::kDefaultAbsTol = 1e-12`, with **nothing loosened for CUDA**. Observed
worst expectation difference: **8.3e-13**, inside that bound. Expectation is
not bitwise identical because the device reduction is a block tree-sum rather
than a sequential loop; its order is fixed, so the value is reproducible run to
run.

Covered: single-qubit gates on every qubit; `RZ` vs `P` (global phase
preserved and the two kept distinguishable); non-trivial `global_phase`;
CX/CZ/SWAP in both operand orders; CCX; parameterized rotations at irrational
angles; `UnitaryOp` at every operand count up to the IR ceiling; randomized
circuits n = 4..12 at fixed seeds; counts at a fixed seed; multi-term
observables including `Y` terms; and refusal of an oversized region.

### Crossover [MEASURED]

Release build, 6 layers of (H on all qubits, T on q0, CX on alternating pairs),
end-to-end `SimulationEngine::execute()` including transfers:

```
                statevector                     expectation
  n     CPU(s)    CUDA(s)  speedup       CPU(s)    CUDA(s)  speedup   VRAM
  4   0.000006   0.000673    0.01x     0.000005   0.000645    0.01x    yes
  8   0.000117   0.001317    0.09x     0.000111   0.001163    0.10x    yes
 10   0.000633   0.002038    0.31x     0.000559   0.001749    0.32x    yes
 12   0.003334   0.002197    1.52x     0.002957   0.002075    1.43x    yes
 14   0.017135   0.002802    6.11x     0.016252   0.002838    5.73x    yes
 16   0.083752   0.008414    9.95x     0.080335   0.007701   10.43x    yes
 18   0.413964   0.032919   12.58x     0.378403   0.032593   11.61x    yes
 20   1.939284   0.145887   13.29x     1.750076   0.125416   13.95x    yes
 22   8.299257   0.616222   13.47x     7.992475   0.535738   14.92x    yes
 24  37.534589   2.628077   14.28x    36.921969   2.311727   15.97x    yes
 26  (skipped)  11.536352        -    (skipped)  10.089468        -    yes
 28  (skipped) (no VRAM)         -    (skipped) (no VRAM)         -     NO
```

**Crossover is n = 12.** Below it the GPU loses (100x at n = 4), dominated by
launch overhead exactly as the cost model predicts. Above it the GPU wins,
reaching ~14-16x at n = 24. n = 28 is correctly refused for device memory.

An earlier probe using **hand-specialised** H and CX kernels reached 57-134x
with a crossover near n = 10. The shipped kernel is generic, as the primitive
API requires, and is correspondingly slower: the honest figures are the ones
above. Specialised kernels are now a benchmark-justified future change, not a
claim made here.

**The Phase 10 gate's "if it is not faster anywhere useful, do not ship it" is
satisfied**: a 14-16x end-to-end speedup from n = 14 to n = 26 is useful, and it
is measured including transfers, not kernel-only.

## 15. Consequences

- The CUDA backend is shipped, with `MENOS_ENABLE_CUDA` defaulting **OFF** so a
  machine without a toolkit builds and passes the gate unchanged.
- `SimulationEngine` gains `backend()`; all five CPU engines return
  `Backend::Cpu`. Justified now that a second backend exists (prime directive 3).
- The GPU is memory-limited below the CPU on this card; that is a property of
  4 GB, not of the implementation.
- The presets pin both `CMAKE_CUDA_COMPILER` and `CUDAToolkit_ROOT`. Removing
  either silently reintroduces a wrong build (section 8).
- Metal, ROCm, multi-GPU and distributed execution remain deferred.

---

## 16. Deferred, with the evidence to re-evaluate each

Recorded at the Phase 10 freeze. None of these is a gap in the gate; each is a
decision made with a reason, and each carries the measurement that would justify
revisiting it.

### 16.1 `time_budget_seconds` stays out of the Python API [DECIDED: internal]

**Decision: not exposed to Python for now.** It remains a C++ `RunOptions`
field.

Inspected before deciding, not assumed:

| Field on `RunOptions` | Bound in `src/python/bindings.cpp`? | Set by the Qiskit `BackendV2`? |
|---|---|---|
| `shots` | yes | yes |
| `want_statevector` | yes | yes |
| `seed` | yes | yes |
| `memory_budget_bytes` | yes | **no** |
| `expectation` | yes | yes (Estimator path) |
| `accuracy_policy` | **no** | no |
| `time_budget_seconds` | **no** | no |

Two findings decide it:

1. **It would be architecturally consistent.** `time_budget_seconds` is a
   direct sibling of `memory_budget_bytes`: both are `std::optional` resource
   budgets on the same already-bound struct. Exposing it needs **one
   `.def_rw` line** and **no new abstraction** -- nanobind already binds
   `std::optional` for the memory budget. So the owner's "do not invent a new
   API abstraction merely to expose it" would not be violated.

2. **It is not necessary.** No Python caller wants it. The Qiskit `BackendV2`
   -- this project's primary entry point -- sets only `shots`, `seed` and
   `want_statevector`, and does not surface `memory_budget_bytes` either,
   although that one *is* bound. `RunOptions` is a low-level escape hatch and
   the Qiskit layer deliberately exposes a curated subset. Adding a binding
   with no caller and no test would be the speculative surface prime directive 2
   forbids.

The Python API therefore stays **backend-agnostic** as well: nothing in it names
CUDA, selects a backend, or reveals that one exists. Backend choice is the
planner's, made from measurements, and that is the only place it is made.

**To revisit:** the trigger is a real caller -- a Python user or the Qiskit
adapter needing a wall-clock constraint. The change is one line in
`src/python/bindings.cpp` beside `memory_budget_bytes`, plus a pytest. Nothing
in the current design has to move.

> **AMENDED by Phase 11 (2026-09-30, `docs/adr/0015` section 9): this deferral
> is lifted.** The revisit trigger named above occurred. Phase 11 makes
> `time_budget_seconds` the threshold for runtime monitoring and replanning, so
> leaving it C++-only would have put that whole feature out of reach of the
> primary entry point. The change was exactly what this section predicted: one
> `.def_rw` line in `src/python/bindings.cpp` and a pytest
> (`python/tests/test_time_budget.py`); nothing in the design moved. The Python
> API remains backend-agnostic, and the Qiskit `BackendV2` adapter still does
> not surface the field.

### 16.2 Heterogeneous per-region backend selection [DEFERRED to Phase 11]

Backend selection applies to a **direct dense plan only**. A `Factorized` or
`CutAndRecombine` plan keeps every region on the CPU, even where one region
would be faster on the GPU.

This is a real limitation, not an oversight, and it is bounded: the
`(Engine, Backend)` machinery already supports it, since
`Region::backend` is per region and `find_execution_engine()` resolves each
region independently. What is missing is the *decision*: choosing per-region
backends means costing a combinatorial assignment rather than one comparison,
and the regions of a factorized plan execute sequentially against one shared
device, so a per-region choice interacts with device-memory residency and
transfer scheduling in ways this phase has measured nothing about.

Phase 11 is runtime monitoring and replanning, which is where a decision that
depends on observed execution belongs. Recorded there as a consideration.

### 16.3 Specialised 1-qubit/2-qubit CUDA kernels [DEFERRED: follow-up optimisation]

The shipped kernel is **generic over operand count**, because the primitive API
requires one kernel that mirrors the CPU primitive for every `k`.

The measured cost of that genericity, both end-to-end including transfers on the
same GPU and the same circuit family:

| kernel | crossover | peak speedup |
|---|---|---|
| hand-specialised H and CX (probe, not shipped) | n ~ 10 | **57-134x** (n = 22) |
| generic k-operand (shipped) | **n = 12** | **14-16x** (n = 24) |

So genericity costs roughly **4-8x** of the available GPU advantage and moves
the crossover right by about two qubits.

That is a large enough margin to be worth reclaiming, and CLAUDE.md's rule that
there is no optimisation without a benchmark showing the bottleneck is now
**satisfied**: `benchmarks/cuda_crossover_benchmark.cpp` is that benchmark, it
is in the repository, and the hardware is recorded.

**Preserved evidence for the evaluation:**
- The generic-kernel table is section 14 of this ADR and is reproducible with
  `menos_cuda_crossover_benchmark`. (Added 2026-10-03: setting
  `MENOS_CROSSOVER_JSON=<path>` also writes the table as JSON, for
  `tools/dashboard/`. The printed table and the measurements are unchanged.)
- The specialised-kernel probe figures are the ones above. The probe itself was
  scratch code and is **not** in the repository; re-measuring it is part of the
  follow-up, and the numbers above are what it must reproduce before any
  specialised kernel is accepted.

**What a follow-up must not do:** add a specialised path without keeping the
generic one as the oracle, or let the two disagree by so much as a bit. The
`-fmad=false` bitwise-identity property (section 10) is a correctness guarantee
that specialised kernels must also hold, since counts depend on it.

### 16.4 Limitations that stand, and are not to be quietly dropped

Every one of these is a statement about what was *not* verified. They are
recorded so a later reader does not mistake silence for coverage.

- **4 GB VRAM (RTX 3050 Laptop).** The device reports **3770 MiB total /
  3681 MiB free**, not the 4096 MiB nameplate.
- **The n = 27 ceiling is arithmetic, not an executed run.** It follows from
  `16 * 2^n <= 3681 MiB`. The largest qubit count actually executed on the
  device is **n = 26**; **n = 28** was executed only far enough to confirm it is
  refused.
- **The GPU is memory-limited BELOW the CPU** here: n = 27 against the CPU
  path's n = 28-29. A property of the card, not of the implementation.
- **One device, one compute capability.** sm_86 only, and `-arch=sm_86` is
  pinned in the presets. No other GPU, driver or toolkit version was tested.
- **No multi-GPU.** One device is used; nothing enumerates or distributes.
- **No aarch64.** x86_64 Linux only.
- **CPU timings above n = 24 were not measured.** Those benchmark cells print
  `(skipped)`; they are absent, never estimated.
- **`MENOS_ENABLE_CUDA=OFF` on a machine with no toolkit at all is not
  independently verified.** It is exercised here only by the CUDA-off dev and
  release stages on a machine that *does* have a toolkit. The default is OFF and
  `verify.sh` skips its CUDA stages when `/usr/local/cuda/bin/nvcc` is absent,
  but that path has not been run on genuinely CUDA-less hardware.
- **Benchmark figures come from a thermally unconstrained laptop** and drift by
  a few percent between runs.

---

## 17. Supported toolkits: CUDA 12.0 and later [MEASURED, added 2026-10-02]

Added on the owner's request to support CUDA toolkits older than 13. The owner
chose **12.0** as the floor for now; CUDA 11.x is explicitly out of scope.

### Why 12.0 is the floor

The CUDA runtime API that MENOS calls (`cudaMalloc`, `cudaMemcpy`,
`cudaMemcpyToSymbol`, `cudaMemGetInfo`, `cudaGetDeviceProperties`, kernel
launch) is unchanged since long before CUDA 11. The binding constraint is the
language: `dense_kernels.cu` is compiled as **C++20** (`CMAKE_CUDA_STANDARD
20`) and uses `std::span` through `kernels/dense_kernels.h`, and `nvcc` accepts
`-std=c++20` only from **12.0**. Supporting 11.x would need the `.cu` interface
rewritten as C++17 (pointer and length instead of `std::span`) plus a GCC 11
host compiler; that is recorded here as the measured cost, not done.

`CMakeLists.txt` now refuses an `nvcc` below 12.0 at configure time. **Not
verified:** no 11.x toolkit was available to trigger that check.

### The silent-mismatch trap is now a configure error

Section 8, trap 2 (headers from one toolkit, `libcudart` from another,
misreading `cudaDeviceProp`) was guarded only by the presets pinning
`CUDAToolkit_ROOT`. With a second toolkit now supported it is a configure-time
error: CMake compares the major version of `nvcc`, of the headers
(`CUDAToolkit_VERSION`), and of the resolved `libcudart.so.<major>`. Both
directions were reproduced on the WSL machine and both now stop configuration:

| Configuration | Before | Now |
|---|---|---|
| nvcc 13.3, no `CUDAToolkit_ROOT` | headers 13.3 + `libcudart.so.12.4.127`, builds silently | `FATAL_ERROR`: libcudart is from CUDA 12, headers are 13.3.73 |
| nvcc 12.4, `CUDAToolkit_ROOT=/usr/local/cuda` | headers 12.4 + `libcudart.so.13.3.29` | `FATAL_ERROR`: libcudart is from CUDA 13, headers are 12.4.131 |

The status line now prints the resolved runtime path as well.

### The `cuda12-dev` / `cuda12-release` presets

They pin Ubuntu's `nvidia-cuda-toolkit` (`/usr/bin/nvcc` 12.4.131, headers and
`libcudart` under `/usr`, so `CUDAToolkit_ROOT=/usr`) and **GCC 13 for the whole
build**, both `CMAKE_CXX_COMPILER` and `CMAKE_CUDA_HOST_COMPILER`. Section 8
rejected a `-ccbin g++-13` split because it puts a compiler boundary between the
`.cu` objects and the rest; compiling everything with GCC 13 removes that
boundary instead of accepting it. GCC 13 is within the project's GCC 12+ floor
(ADR 0001), and the full warning set with `-Werror` compiled cleanly under it.

### Why this matters on the WSL machine

The Phase 12 verification machine (WSL Ubuntu 26.04, RTX 4060 Laptop GPU,
compute capability 8.9, Windows driver **566.14**, which runs CUDA up to
**12.7**) cannot run the CUDA 13.3 build: every GPU test in `cuda-dev` /
`cuda-release` skips with "no CUDA device available" (`docs/BUILD_PLAN.md`,
Phase 12). The CUDA 12.4 build is the first on that machine whose GPU tests
execute.

### Evidence (WSL machine, 2026-10-02, no source change to any `.cpp`/`.cu`/`.h`)

| Stage | Result |
|---|---|
| `cuda12-release` configure | `nvcc 12.4.131 at /usr/bin/nvcc, runtime /usr/lib/x86_64-linux-gnu/libcudart.so.12.4.127, architectures 86` |
| `cuda12-release` build | exit 0, `-Werror` |
| `cuda12-release` ctest | **584/584 passed**, 1 skipped by design (below) |
| `cuda12-release` GPU suites run directly | `CpuCudaDifferentialTest` 11 passed; `menos_plan_tests *Cuda*` 13 passed; none skipped |
| `cuda12-dev` build | exit 0 |
| `cuda12-dev` ctest, before the suppression below | 459 passed, **125 failed**: every failure is the same `LeakSanitizer: 320 byte(s) leaked in 2 allocation(s)` inside `/usr/lib/wsl/lib/libcuda.so.1` (`cuGetProcAddress_v2` under `pthread_once`), with 0 gtest-level failures and 0 MENOS frames in any leak stack. The same WSL-driver leak already fails `cuda-dev` here (`docs/BUILD_PLAN.md`, Phase 12) |
| `cuda12-dev` ctest, `detect_leaks=0` (ASan+UBSan otherwise on) | **584/584 passed**; `CpuCudaDifferentialTest` + `CudaExecutionMonitorTest` 19 passed, 1 skipped by design |

The by-design skip is `CudaExecutionMonitorTest.TenAbandonsOfAnOversubscribingTotalNeverExhaustTheDevice`:
it needs a device too small to hold ten 512 MiB states at once, and this
device has about 7.4 GB free.

The binary is built for `sm_86` and runs on the 8.9 device by same-major binary
compatibility. A native `sm_89` build was not tested.

### The WSL driver leak is suppressed by path [MEASURED, owner-approved]

The owner approved a LeakSanitizer suppression for that leak. It is
`scripts/lsan-wsl-libcuda.supp`, applied through `LSAN_OPTIONS` in the
`cuda-dev` and `cuda12-dev` test presets only, and it holds one pattern:
`leak:/usr/lib/wsl/lib/libcuda.so`. The full path is deliberate: a native Linux
`libcuda` is not covered, and neither is any leak whose stack does not pass
through the WSL shim. LeakSanitizer still prints a "Suppressions used" line,
so the suppression never fires invisibly.

Checked with a standalone ASan program that calls `cudaGetDeviceCount`:

| Run | Result |
|---|---|
| no suppression | `320 byte(s) leaked in 2 allocation(s)`, exit 1 |
| suppression | `Suppressions used: 2  320 /usr/lib/wsl/lib/libcuda.so`, exit 0 |
| suppression, plus a deliberate `new char[123]` leak in `main` | `123 byte(s) leaked`, attributed to `leak.cpp:8`, exit 1 |

With the suppression, `ctest --preset cuda12-dev` passed **584/584**.

### Not verified

- CUDA 12.0 to 12.3, and 12.5 to 12.9: only 12.4.131 was available.
- A CUDA 12 toolkit installed from NVIDIA under `/usr/local/cuda-12.x` (the
  presets pin the Ubuntu package's layout).
- The 12.0 floor check firing, since no 11.x toolkit was available.
- The Python wheel built against CUDA 12: its build takes the same CMake
  settings through `CMAKE_ARGS`, but it was not run.
