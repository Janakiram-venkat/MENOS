# ADR 0022: Phase 18 — Native Windows (MSVC)

## Status

**Accepted for implementation (2026-10-13); gate not yet passed.** The owner started Phase 18 on 2026-10-13 as the next phase, answered section 9 the same day and approved the implementation plan. Branch `phase-18-windows-native`, from `main` at `ff74397`. This ADR supersedes the sentence "Windows is not supported" in `docs/adr/0001` for Windows x64 with MSVC, CPU only; everything else in ADR 0001 stands.

Labels follow `docs/adr/0014`–`0021`: **[MEASURED]**, **[DERIVED]**, **[MENOS]** (a proposed design decision) and **[OPEN]** (an owner decision, section 9).

---

## 1. Context

MENOS refused to build on Windows on purpose. What stood in the way on 2026-10-13:

| Blocker | Where | What exists today |
|---|---|---|
| Platform guard | `src/plan/hardware_profile.cpp` | `#error` on anything but Linux and macOS; host memory read with `sysconf` (Linux) or `sysctlbyname` (macOS) |
| Fork guard | `src/kernels/cpu/thread_pool.cpp` | `<unistd.h>`, `getpid()` and `pid_t`, to detect a forked child |
| Warning flags | `CMakeLists.txt` (`MENOS_WARNING_FLAGS`) | GCC/Clang syntax only (`-Wall … -Werror`) |
| Floating-point contraction | `src/kernels/CMakeLists.txt` | `-ffp-contract=off`, GCC/Clang syntax only |
| LAPACKE lookup | `src/common/CMakeLists.txt` | `find_path` limited to `/usr` paths; `find_library(lapacke)` |
| Presets | `CMakePresets.json` | Linux only (GCC sanitizer flags, `/usr` CUDA paths) |
| Verification | `scripts/verify.sh` | bash, Linux tools |
| Wheels | `.github/workflows/build-wheels.yml` | manylinux only; no wheel test command |

Everything else was measured portable by a full search of the source: no OpenMP, `getopt`, `mmap`, `pthread_*`, `__attribute__` or `__int128`. The RNG is MENOS's own (`src/common/rng.h`), so seeded results do not depend on the standard library's distributions. The CLI is plain C++. Over-aligned allocation uses `operator new(std::align_val_t)`. The thread pool's `std::atomic::wait`/`notify` is in MSVC's STL.

Constraints every change keeps:
- Linux and macOS builds are unchanged: the same flags, the same lookup, the same presets, the same `verify.sh`.
- Nothing is silently approximated and no acceptance tolerance changes (section 9, Q6).
- `plan_job` stays the only planner; the memory probe feeds it exactly as on Linux.

## 2. Host-memory probe — [MENOS]

`detect_hardware_profile()` gains a `_WIN32` branch: `GlobalMemoryStatusEx` and its `ullTotalPhys`, the total physical memory the OS can use. This is the same quantity as Linux's `_SC_PHYS_PAGES × _SC_PAGE_SIZE`. A failed call throws through `common::ensure`, as the other branches do. The planner's default budget (`kDefaultMemoryBudgetFraction` = 0.5 of it) is unchanged.

Regression test: on Windows the probe's figure is positive and no larger than `GetPhysicallyInstalledSystemMemory`, an independent source (the installed RAM, of which the OS-visible total is a subset).

The thread pool's fork guard is compiled only where `fork` exists. Windows has no `fork`, so the guard has nothing to detect there.

## 3. MSVC toolchain — [MENOS]

- **Compiler.** MSVC 14.50 (Visual Studio Build Tools 2026) is the verified version; `/std:c++20` through `CMAKE_CXX_STANDARD 20`.
- **Warnings.** `MENOS_WARNING_FLAGS` under MSVC: `/W4 /WX /permissive- /Zc:__cplusplus /utf-8`. Warnings found by `/W4` are fixed in the source with value-preserving casts; none is suppressed globally.
- **Floating point.** `/fp:precise`, which does not contract `a*b+c` into an FMA. `/fp:contract` and `/fp:fast` are never used. This is the MSVC counterpart of `-ffp-contract=off`, and keeps the specialised kernels bitwise identical to the generic kernel (Phase 15 invariant).
- **Presets.** `windows-dev` (Ninja, `cl`, Debug, `/fsanitize=address`, with the runtime checks `/RTC1` and incremental linking turned off, both incompatible with ASan) and `windows-release` (Ninja, `cl`, Release). Both carry a `hostSystemName == Windows` condition and need a Visual Studio developer environment, which `scripts/verify-windows.ps1` enters.
- **CUDA.** `MENOS_ENABLE_CUDA=ON` with MSVC is a configure-time `FATAL_ERROR` naming this deferral. It is never silently turned off.
- **MSVC only.** On Windows any other compiler is a configure-time `FATAL_ERROR`. **[MEASURED]** on 2026-10-13: a `cibuildwheel` run outside the developer environment picked up MinGW's CMake and GCC 15.2 from `C:\mingw64\bin`, configured and built without complaint, and the resulting cp310 extension then failed 9 of 260 Python tests (an `InternalError` in `append_batch`, a memory budget read as 0 bytes, different seeded counts). GitHub's Windows runners also have MinGW on `PATH`, so the wheel build pins `CMAKE_GENERATOR=Ninja` and `CC`/`CXX=cl` and runs inside the developer environment.

## 4. LAPACK on Windows — [MENOS]

MENOS calls one LAPACK routine, `LAPACKE_zgesdd` (`src/common/svd.cpp`). MSVC has no Fortran compiler, so the Linux wheel job's approach (building reference LAPACK from source) is not available.

**Source: OpenBLAS 0.3.34, official x64 binary release** — `OpenBLAS-0.3.34-x64.zip`, SHA-256 `e9cb6134541f36c27346d5fc5995652f060fba227cebbbabcbda5a5a44d7c76b`, released 2026-07-16. **[MEASURED]** on 2026-10-13:
- It contains `bin/libopenblas.dll`, the import library `lib/libopenblas.lib` and `include/lapacke.h`.
- `dumpbin /exports` lists `LAPACKE_zgesdd`; `dumpbin /dependents` lists only `KERNEL32.dll` and `msvcrt.dll`, so the DLL needs no other runtime.
- It is LP64: `openblas_config.h` does not define `OPENBLAS_USE64BITINT`. The `x64-64` zip of the same release is ILP64 and is not used; `lapack_int` must be 32 bits to match `svd.cpp`'s casts.

`scripts/fetch-openblas-windows.ps1` downloads the zip and OpenBLAS's `LICENSE` (BSD-3-Clause) at tag `v0.3.34`, checks both SHA-256 hashes, and unpacks into `build/_deps/openblas-0.3.34` (git-ignored). CMake finds it through `MENOS_OPENBLAS_ROOT`. The DLL is copied next to every test and CLI executable so `ctest` runs without changing `PATH`.

OpenBLAS runs its own threads for BLAS calls. MENOS still does not set `OPENBLAS_NUM_THREADS` (Phase 15 invariant).

## 5. Python package and wheels — [MENOS]

- On Windows the build installs `libopenblas.dll` and its licence next to `_core.pyd` in the `menos` package. Python loads extension modules with the module's own directory on the DLL search path, so the extension finds OpenBLAS in an editable install and in a wheel alike.
- Wheels: `cibuildwheel` with `[tool.cibuildwheel.windows]`, CPython 3.10–3.14, `AMD64`. `delvewheel repair --exclude libopenblas.dll` bundles the MSVC C++ runtime, which Python itself does not ship, and fails on any dependency it cannot resolve; OpenBLAS is excluded because CMake already installs it beside `_core.pyd`, so the wheel carries exactly one copy. **[MEASURED]** `delvewheel show` on the cp314 wheel: it vendors `msvcp140.dll` and `msvcp140_atomic_wait.dll` (the thread pool's `std::atomic::wait`); every other import is a system, UCRT or Python DLL. The test command runs the Python suite against each installed wheel.
- `.github/workflows/build-wheels.yml` gains a `windows` job; the Linux job is unchanged.

## 6. Order of work

1. This ADR and the `docs/BUILD_PLAN.md` specification.
2. Memory probe, fork guard and the regression test.
3. MSVC flags, presets and `/W4` fixes.
4. OpenBLAS fetch and LAPACKE discovery.
5. Python packaging and the CLI-binary path in the Python tests.
6. `scripts/verify-windows.ps1`.
7. `cibuildwheel` configuration and the workflow job.
8. Documentation.
9. Gate evidence (section 10).

## 7. Out of scope

- CUDA on Windows (refused at configure time).
- Windows ARM64; MinGW; clang-cl.
- Publishing to PyPI.
- clang-format and clang-tidy on Windows: they stay in the Linux `verify.sh`, which checks the same sources.
- Processor groups: on a machine with more than 64 logical CPUs the thread pool may use only one group. Not tested.
- CPU-name detection in the Windows benchmark environment (it reports `unknown-cpu`).

## 8. Risks

| Risk | How it is caught |
|---|---|
| `/W4 /WX` exposes a real narrowing bug | Each fix is value-preserving or reported to the owner separately; the full test suite runs on both platforms |
| The MSVC runtime's `sin`/`cos`/`exp`, or OpenBLAS against reference LAPACK, differs from Linux in the last bits | Tests run unmodified on Windows; any such failure is brought to the owner (Q6) |
| ASan and MSVC Debug defaults conflict | The `windows-dev` preset removes `/RTC1` and incremental linking explicitly |
| A wheel misses a DLL | `delvewheel` fails on unresolved dependencies, and each wheel is tested in a clean environment |
| A Linux behaviour changes by accident | The Linux `scripts/verify.sh` is part of the gate (criterion 7) |

## 9. Owner decisions needed

- **Q1 — Phase number.** Proposed: Phase 18, with the proposed interoperability roadmap renumbered to 19–26.
- **Q2 — CUDA.** Proposed: CPU only; CUDA on Windows deferred.
- **Q3 — Python versions.** Proposed: CPython 3.10–3.14, as on Linux.
- **Q4 — Sanitizers.** Proposed: MSVC ASan in Debug; UBSan and LSan stay with Linux.
- **Q5 — LAPACK source.** Proposed: the official OpenBLAS x64 binary release, pinned. **Alternatives:** vcpkg (needs MSYS2 gfortran for reference LAPACK), Intel MKL (hundreds of MB per wheel, Intel licence).
- **Q6 — Cross-platform numerics.** Proposed: a Windows failure caused only by last-bit differences from Linux is brought to the owner; no tolerance changes.
- **Q7 — The gate.** As written in `docs/BUILD_PLAN.md`, Phase 18.

### Owner answers (2026-10-13)

1. **Q1:** as proposed.
2. **Q2:** as proposed: CPU only.
3. **Q3:** as proposed: 3.10–3.14.
4. **Q4:** as proposed.
5. **Q5:** as proposed: pinned OpenBLAS release.
6. **Q6:** as proposed: stop and bring it to the owner.
7. **Q7:** approved with the implementation plan.

---

## 10. Gate evidence

To be recorded at the end of the phase.
