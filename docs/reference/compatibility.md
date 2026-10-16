# Compatibility

What MENOS was built and tested against. "Tested" means exercised by `scripts/verify.sh` (Linux) or `scripts/verify-windows.ps1` (Windows) on the development machine.

## Software

| Component | Version | Status |
|---|---|---|
| Python | **3.10–3.14** | Required (`>=3.10`); 3.14 in both verification scripts, 3.10–3.14 by the Windows wheel tests |
| Qiskit | **2.5.2** | Pinned exactly; other versions untested |
| Qiskit Aer | **0.17.2** | Optional (`menos[noise]`), for noise models; pinned exactly and checked against Qiskit 2.5.2 |
| NumPy | Whatever installs with your Python | Required |
| CMake | 3.25 or newer | 4.2.3 used |
| C++ compiler | GCC 12+, Clang 16+, AppleClang 15+ (C++20); MSVC on Windows | GCC 15.2.0 and MSVC 14.50 used |
| LAPACK, LAPACKE, BLAS | Development packages; OpenBLAS 0.3.34 (LP64, pinned) on Windows | Required to build |
| `scikit-build-core` | 1.0.3 | Build-time, pinned |
| `nanobind` | 3.1.0 | Build-time, pinned |
| GoogleTest | v1.18.0 (pinned commit) | Fetched at configure time; used by tests only |
| nlohmann/json | v3.12.0 | Fetched at configure time; used by tests only |

## Platforms

| Platform | Status |
|---|---|
| Linux x86-64 | **Verified** |
| Linux aarch64 | Intended, not verified |
| macOS arm64 | Intended, not verified |
| Windows x64 (MSVC) | **Verified**, CPU only |
| Windows ARM64 | Not supported |

## GPU

| Component | Version / status |
|---|---|
| NVIDIA GPU | Compute capability 8.6 verified (RTX 3050 Laptop GPU); 8.9 verified running code built for 8.6 (RTX 4060 Laptop GPU) |
| CUDA toolkit | 12.0 or newer required; 13.4 and 12.4 verified |
| NVIDIA driver | 595.91.07 verified with toolkit 13.4 (advertises CUDA 13.2); 566.14 verified with toolkit 12.4 under WSL (advertises CUDA 12.7) |
| CUDA on aarch64, macOS or native Windows | Not supported (on Windows, use WSL) |
| AMD ROCm, Apple Metal | Not implemented |

## Qiskit interfaces

| Interface | Implemented |
|---|---|
| `BackendV2` | Yes (`MENOSBackend`) |
| `JobV1` | Yes (`MENOSJob`) |
| `BaseEstimatorV2` | Yes (`MENOSEstimator`) |
| `BaseSamplerV2` | **No** |
| Provider | **No** |
| `BackendV1` | No |

## OpenQASM

The command-line tool reads OpenQASM 2 and OpenQASM 3, chosen by the program's header (see [Command Line](/build/command-line#openqasm-support)). OpenQASM 3 is imported for its static-circuit subset, with `stdgates.inc` gates as Qiskit defines them; `reset`, control flow and timing are refused with their location. From Python, `menos.from_qasm3(text, inputs)` returns a `Circuit` (since Phase 19); there is no Python entry point for OpenQASM 2. Results agree with Qiskit's own OpenQASM 3 import (`qiskit.qasm3.loads`) within 1e-12, global phase included.

## Verification

```python
import sys
from importlib.metadata import version

import menos

assert sys.version_info >= (3, 10)
assert version("qiskit") == "2.5.2"
assert menos.__version__ == version("menos")
```
