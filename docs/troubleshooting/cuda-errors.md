# CUDA Errors

A quick index of CUDA-related errors. The full explanations, with causes and fixes, are in [Troubleshooting CUDA](/gpu/troubleshooting-cuda).

| Error text | Where to look |
|---|---|
| `cuda: cudaMemcpyToSymbol (matrix) failed: the provided PTX was compiled with an unsupported toolchain. (cudaError_t 222)` | The package was built without `CMAKE_CUDA_ARCHITECTURES`. [See the fix](/gpu/troubleshooting-cuda#the-provided-ptx-was-compiled-with-an-unsupported-toolchain) |
| `cuda: cudaMalloc failed: out of memory` | Device memory was claimed after MENOS measured it. [See the fix](/gpu/troubleshooting-cuda#cudamalloc-failed-out-of-memory) |
| The job runs on the CPU although a GPU is present | [See why](/gpu/troubleshooting-cuda#the-job-always-runs-on-the-cpu) |
| CMake picks the wrong `nvcc` | [See the fix](/gpu/troubleshooting-cuda#nvcc-or-the-toolkit-is-wrong) |
| Device properties look wrong | [See the fix](/gpu/troubleshooting-cuda#the-device-looks-wrong) |

## How CUDA errors surface

- A request that does not fit the **device budget** never errors: the planner keeps the region on the CPU.
- A CUDA failure that happens *despite* the budget check surfaces as `InternalError` with the CUDA error text, because it means something MENOS could not predict went wrong.
- A build with no CUDA support never attempts GPU work.

## See also

[GPU Memory](/gpu/gpu-memory) · [CUDA Requirements](/gpu/cuda-requirements)
