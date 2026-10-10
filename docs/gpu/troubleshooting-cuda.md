# Troubleshooting CUDA

Each entry is a **symptom**, its **cause**, and a **solution**. The messages are quoted from real failures.

## The job always runs on the CPU

| | |
|---|---|
| **Symptom** | `explain()` shows `backend=cpu` for a large dense job |
| **Cause, most common** | The installed build has no CUDA support: a default install never does |
| **Solution** | Rebuild with `MENOS_ENABLE_CUDA=ON` ([CUDA Installation](/gpu/cuda-installation)) |

Other reasons the planner keeps a job on the CPU, all correct behaviour:

| Cause | How to tell |
|---|---|
| The circuit is too small; launch overhead wins | Fewer than about 12 qubits |
| The plan is not a dense region (stabilizer, MPS, factorized, cut) | `strategy` and `engine` in the report |
| The state does not fit in device memory | The region needs more than the device's free memory |
| No usable GPU at run time | `nvidia-smi` fails or shows no device |
| The CUDA candidate was slower by the calibrated model | Compare the two estimates |

## `the provided PTX was compiled with an unsupported toolchain`

| | |
|---|---|
| **Symptom** | The first GPU job raises `InternalError`: `cuda: cudaMemcpyToSymbol (matrix) failed: the provided PTX was compiled with an unsupported toolchain. (cudaError_t 222)` |
| **Cause** | The package was built without `CMAKE_CUDA_ARCHITECTURES`, so the kernels were compiled for CMake's default architecture, not your GPU's. The driver then tries to recompile from PTX and cannot |
| **Solution** | Reinstall with `-DCMAKE_CUDA_ARCHITECTURES=<your compute capability without the dot>` (for example `86`). Find it with `nvidia-smi --query-gpu=compute_cap --format=csv` |

## `cudaMalloc failed: out of memory`

| | |
|---|---|
| **Symptom** | `InternalError`: `cuda: cudaMalloc failed: out of memory` |
| **Cause** | Something else claimed device memory after MENOS read the free figure, or the state is close to the budget |
| **Solution** | Close GPU-heavy applications, then retry; use fewer qubits; or run in a fresh process so the free figure is read again |

## `nvcc` or the toolkit is wrong

| | |
|---|---|
| **Symptom** | CMake reports an unexpected `nvcc` version or a host-compiler error such as the compiler being too new |
| **Cause** | More than one CUDA toolkit is installed and CMake picked the first `nvcc` on `PATH` |
| **Solution** | Pass `-DCMAKE_CUDA_COMPILER=/path/to/nvcc` and `-DCUDAToolkit_ROOT=/path/to/toolkit` explicitly |

## The device looks wrong

| | |
|---|---|
| **Symptom** | The device reports implausible properties, such as a single multiprocessor |
| **Cause** | The build compiled against one toolkit's headers and linked a different toolkit's runtime library |
| **Solution** | Set `CUDAToolkit_ROOT` to the toolkit you compile with, so the headers and the library agree |

## CUDA tests all skip in a sanitizer build

| | |
|---|---|
| **Symptom** | With the `cuda-dev` preset, the test suite passes but no CUDA test actually ran |
| **Cause** | AddressSanitizer's memory layout collides with the CUDA driver's address reservation, so the driver cannot initialize and every CUDA test skips |
| **Solution** | The project's test presets set `ASAN_OPTIONS=protect_shadow_gap=0`. If you run the binary by hand, set it too |

## No GPU is detected

| | |
|---|---|
| **Symptom** | `nvidia-smi` fails or lists no device |
| **Cause** | Driver not installed or not loaded, or no NVIDIA GPU |
| **Solution** | Install the NVIDIA driver. MENOS is unaffected: it falls back to the CPU |

## Still stuck

Collect the output of `nvidia-smi`, the exact build command, and the plan report (`explain()`), and compare with [CUDA Requirements](/gpu/cuda-requirements).
