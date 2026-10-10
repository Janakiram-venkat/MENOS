# Installation

MENOS is published on PyPI as [`menos`](https://pypi.org/project/menos/). The published wheel is prebuilt, so installing it compiles nothing.

## Install from PyPI

With `pip`:

```bash
pip install menos
```

With `uv`, inside a project:

```bash
uv add menos
```

To use `MENOSBackend`, `MENOSEstimator` and `MENOSSampler`, install the `qiskit` extra, which pins Qiskit 2.5.2:

```bash
pip install "menos[qiskit]"
# or
uv add "menos[qiskit]"
```

Without it you can still use the [native API](/build/native-api).

::: warning What the PyPI wheel covers
Version 0.2.0 ships one wheel: **CPython 3.14, Linux x86-64** (manylinux 2.28, so glibc 2.28 or newer). It is **CPU-only**. No source distribution is published, so on any other platform, Python version or architecture `pip` finds no matching distribution; [install from source](#install-from-source) instead. For GPU support, [build from source with CUDA](/gpu/cuda-installation).
:::

You need **Python 3.14**. The package requires `>=3.14` and 3.14 is the only version that has been tested. If your system does not provide it, [`uv`](https://docs.astral.sh/uv/) can install it:

```bash
uv python install 3.14
```

Then [verify the installation](#verify).

## Install from source

Build from source if you need the CUDA backend, are on a platform the wheel does not cover, or will modify MENOS. MENOS is a C++ library with a Python extension, so a source install compiles native code; expect it to take a few minutes the first time.

### 1. Install the system prerequisites

You need a C++20 compiler, CMake 3.25 or newer, `git`, and the LAPACK and LAPACKE development libraries (the matrix-product-state engine uses LAPACKE for its singular value decompositions).

On Ubuntu:

```bash
sudo apt install build-essential cmake git liblapacke-dev liblapack-dev libblas-dev
```

On other Linux distributions, install the equivalent packages. On macOS, LAPACK is found through the Accelerate framework; note that macOS is [not verified](/introduction/supported-platforms).

You also need **Python 3.14** (see above).

### 2. Get the source

Clone the repository, or unpack a source archive, and change into its root directory (the one containing `pyproject.toml`).

### 3. Create an environment and install

With `uv`:

```bash
uv venv .venv --python 3.14
uv pip install --python .venv/bin/python ".[qiskit]"
```

With plain `pip`:

```bash
python3.14 -m venv .venv
.venv/bin/pip install ".[qiskit]"
```

The `qiskit` extra installs Qiskit 2.5.2, which `MENOSBackend` and `MENOSEstimator` require. Without it you can still use the [native API](/build/native-api).

::: tip What the build needs from the network
Building requires network access. During configuration CMake downloads GoogleTest and nlohmann/json from GitHub at pinned versions. **Both are used only by the test suite**, but they are fetched even for a Python-only install, and the build fails without them. Python build tools (`scikit-build-core` 1.0.3 and `nanobind` 3.1.0) are fetched by the package installer.
:::

## Verify

Whichever way you installed, run this with the environment's Python (`.venv/bin/python` for the source steps above, or `uv run python` in a uv project):

```bash
python -c "import menos; print(menos.__version__)"
```

Then follow [Verify Installation](/get-started/verify-installation), which runs a real circuit.

## Building for development

If you will modify MENOS, use an editable install and the project's presets:

```bash
uv venv .venv --python 3.14
uv pip install --python .venv/bin/python -e ".[test]"
.venv/bin/python -m pytest python/tests --deselect python/tests/test_wheel_smoke.py
```

The C++ library and its tests build separately with CMake presets:

```bash
cmake --preset release
cmake --build --preset release
ctest --preset release --output-on-failure
```

`scripts/verify.sh` runs the complete verification (formatting, static analysis, both presets, the Python suite, a wheel build and a clean-environment smoke test). It needs `clang-format` and `clang-tidy` in addition to the prerequisites above.

## Enabling CUDA

Neither the PyPI wheel nor a default source install has GPU support. See [CUDA Installation](/gpu/cuda-installation) for how to build with it.

## If installation fails

See [Installation Problems](/troubleshooting/installation) and [Build Errors](/troubleshooting/build-errors).
