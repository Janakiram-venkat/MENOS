# Supported Platforms

This page states what the project has actually been verified on. "Intended to work" and "verified" are different claims and are kept apart here.

## Verified

| Component | Platform | Status |
|---|---|---|
| CPU engines, planner, runtime | Linux, x86-64 | **Verified.** The full verification script (`scripts/verify.sh`) passes: C++ unit and integration tests in Debug (with sanitizers) and Release, the Python suite, a wheel build, and a clean-environment wheel smoke test. |
| CUDA dense backend | Linux, x86-64, NVIDIA GPU, CUDA toolkit **12.0 or newer** | **Verified** with CUDA toolkit 13.4 and driver 595.91.07 (RTX 3050 Laptop, compute capability 8.6), and with CUDA toolkit 12.4 and driver 566.14 under WSL (RTX 4060 Laptop, compute capability 8.9, built for 8.6). CUDA 12.0 to 12.3 are not tested. |

## Distribution

| Channel | What it covers |
|---|---|
| PyPI (`pip install menos`, `uv add menos`) | Version 0.2.0: one prebuilt wheel for **CPython 3.14, Linux x86-64** (manylinux 2.28), **CPU-only**. No source distribution is published |
| Source build | Everything on this page, including the CUDA backend |

## Intended but not verified

| Platform | Notes |
|---|---|
| Linux, aarch64 | The code has no x86-specific dependencies, but it has not been built or tested. **The CUDA backend is not verified on aarch64.** |
| macOS, arm64 | The host-memory probe has a macOS code path and LAPACK is found through the Accelerate framework. **Not built or tested.** There is no CUDA backend on macOS. |

## Not supported

- **Windows.** The source deliberately refuses to build on anything other than Linux and macOS.
- **AMD (ROCm) and Apple (Metal) GPUs.** Not implemented. Only NVIDIA CUDA is supported.
- **Multiple GPUs.** One CUDA device is used.

## Language and library versions

| | Version | Basis |
|---|---|---|
| Python | **3.14** | The package requires `>=3.14`; 3.14 is the only version tested. |
| Qiskit | **2.5.2** | Pinned exactly by the `qiskit` and `test` extras. Other versions are untested. |
| C++ | C++20 | GCC 12+, Clang 16+ or AppleClang 15+ is the stated minimum; GCC 15.2 is what was used. |
| CMake | 3.25+ | 4.2.3 was used. |

See [Requirements](/get-started/requirements) for the complete list and [Compatibility](/reference/compatibility) for the reference table.
