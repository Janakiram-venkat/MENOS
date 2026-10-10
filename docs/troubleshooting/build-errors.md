# Build Errors

Each entry is a **symptom**, its **cause**, and a **solution**. The exact wording of messages from your compiler and from CMake depends on their versions; the entries describe the situation rather than quote every tool's text.

## Missing LAPACK or LAPACKE

| | |
|---|---|
| **Symptom** | CMake fails while configuring, reporting that it could not find LAPACK, or could not find `lapacke.h` |
| **Cause** | The matrix-product-state engine needs LAPACKE for its singular value decompositions, and the development packages are not installed |
| **Solution** | On Ubuntu: `sudo apt install liblapacke-dev liblapack-dev libblas-dev`. On macOS LAPACK comes from the Accelerate framework (macOS is [unverified](/introduction/supported-platforms)) |

## Compiler too old

| | |
|---|---|
| **Symptom** | Compile errors about unsupported C++20 features |
| **Cause** | MENOS needs C++20: GCC 12+, Clang 16+, or AppleClang 15+ |
| **Solution** | Install a newer compiler and tell CMake to use it (`CXX=g++-13` or `-DCMAKE_CXX_COMPILER=…`) |

## CMake too old

| | |
|---|---|
| **Symptom** | CMake stops immediately, saying a newer version is required |
| **Cause** | The project requires CMake 3.25 or newer |
| **Solution** | Install a newer CMake (for example from your distribution's backports, or `pip install cmake`) |

## Unsupported operating system

| | |
|---|---|
| **Symptom** | `error: "menos supports only Linux and macOS"` |
| **Cause** | The source refuses to build on other platforms, including Windows |
| **Solution** | Build on Linux or macOS. Windows is not supported |

## `scikit-build-core` or `nanobind` not found

| | |
|---|---|
| **Symptom** | The Python build fails before compiling, about a missing build backend |
| **Cause** | The build tools are fetched by the installer into an isolated environment; this fails if you disabled build isolation without installing them |
| **Solution** | Install with default isolation, or first install the pinned tools: `scikit-build-core==1.0.3` and `nanobind==3.1.0` |

## Out of memory while compiling

| | |
|---|---|
| **Symptom** | The compiler is killed partway through the build |
| **Cause** | Parallel compilation of large translation units on a machine with little memory |
| **Solution** | Build with fewer parallel jobs, for example `cmake --build --preset release -j2` |

## `clang-format` or `clang-tidy` not found

| | |
|---|---|
| **Symptom** | `scripts/verify.sh` fails at its first stages |
| **Cause** | The full verification script runs formatting and static analysis, which need those tools |
| **Solution** | Install `clang-format` and `clang-tidy`. They are development-only and are not needed to use MENOS |

## CUDA build errors

See [Troubleshooting CUDA](/gpu/troubleshooting-cuda).

## See also

[Installation Problems](/troubleshooting/installation) · [Requirements](/get-started/requirements)
