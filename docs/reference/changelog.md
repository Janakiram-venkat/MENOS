# Changelog

MENOS was built in phases, each passing an acceptance gate before the next began. The package version is **0.2.0**. The repository has no release tags, so this page records the development history by phase rather than by tagged release.

## Phase 17: API surface

- `RunOptions.backend` (C++ and Python, `menos.Backend`), the `MENOSBackend` run option `backend` and the CLI flag `--backend cpu|cuda` force the CPU or CUDA backend. A CUDA request that cannot be honoured is refused, never run on the CPU.
- `MENOSBackend` takes `time_budget_seconds`; abandoned attempts appear in the result metadata as `menos_replans`, and `Result.replans` exposes them in Python.
- The CLI takes `--time-budget SECONDS`.
- Each dense region of a `factorized` plan runs on CPU or CUDA, whichever the calibrated model says is faster (measured 1.31–1.63× faster on factorized plans of dense blocks than keeping every region on the CPU). Cut-plan fragments stay on the CPU.
- A time budget that is not finite and greater than 0 now raises `InvalidCircuitError`. Before, NaN was silently ignored.

## Phase 16: specialised CUDA kernels

- Specialised 1- and 2-qubit CUDA kernels, bitwise identical to the generic kernel, which stays for larger gates. They make no host-to-device copy per gate.
- Small CUDA circuits run about 1.6–2× faster end to end. Large ones (22–26 qubits) run about 1.2× faster, because the GPU's double-precision rate is the limit.

## Phase 15: dense CPU engine performance

- Multi-threaded gate application on a MENOS-owned thread pool, used by the dense, density-matrix and trajectory engines. Results are bitwise identical for every thread count.
- Specialised 1- and 2-qubit dense kernels, bitwise identical to the generic kernel.
- `RunOptions.max_threads` (C++ and Python) caps the thread count; the default is every hardware thread.
- The benchmark dashboard records 10 repeats per point, the spread beside the median, and the machine's power state.

## Phase 14: readout mitigation, batch execution and SamplerV2

- `run_batch`: runs several circuits, each planned by `plan_job`, under independent derived seeds.
- Opt-in readout-error mitigation (`run_readout_mitigated`): calibration circuits, exact tensored inversion over all outcomes, a reported standard error per outcome and the overhead gamma. Never labelled exact.
- `MENOSSampler`, a Qiskit `SamplerV2` with `noise_model`, `seed` and `readout_mitigation`.

## Phase 13: noisy trajectory simulation

- The trajectory engine: Monte-Carlo sampling of noisy circuits, one pure state per trajectory, with exact Kraus branch probabilities. Results are flagged not exact, and expectation values carry a measured standard error.
- Opt-ins: `RunOptions.expectation_precision` for expectation values and `RunOptions.noise_method = NoiseMethod.Auto` for counts. Without them, noisy jobs plan exactly as in Phase 12.
- The planner chooses between the density matrix and trajectories by cost; a density-matrix overrun of a time budget can replan to trajectories.
- `Result.expectation_standard_error`; `MENOSEstimator` returns genuine `stds` and takes a `seed`; `MENOSBackend` takes `noise_method`.
- CUDA toolkits 12.0 and later are supported.

## Phase 12: exact noisy simulation

- Noise channels and readout errors in KIR, validated (trace preservation within `1e-12`).
- The density-matrix engine: exact noisy simulation, planned only for jobs that contain noise.
- Qiskit Aer `NoiseModel` support in `MENOSBackend` and `MENOSEstimator` (`noise_model=`), for verified noise types.
- `Circuit.append_channel` and a `readout_error` argument on `Circuit.append_measure` in the native API.

## 0.2.0, the state after Phase 11

| Phase | Delivered |
|---|---|
| 0 | Foundation: build system, toolchain, verification script, error types |
| 1 | KIR, dense CPU engine, plan IR, runtime, result and `explain()` |
| 2 | Python package and a standards-conforming Qiskit `BackendV2` |
| 3 | Factorization, the work-unit cost model, and the first real planner decision |
| 4 | Stabilizer engine and the common engine interface |
| 5 | Observables, light-cone reduction, Pauli propagation, and expectation values |
| 6 | MPS engine and the accuracy policy (exact by default) |
| 7 | Command-line tool and OpenQASM 2 parser |
| 8 | Tensor-network engine (expectation values) |
| 9 | Cut and recombine: exact wire cutting for expectation values |
| 10 | CUDA dense backend, the shared primitive API, hardware calibration, backend selection by measured seconds, and strict time budgets at plan time |
| 11 | Runtime monitoring and replanning; `time_budget_seconds` exposed in Python |

## Notable behaviours introduced along the way

- **Exactness by default** (Phase 6): every engine reachable from Python is exact.
- **Arbitrary-width counts** (Phase 3): classical registers have no 64-bit ceiling.
- **Estimator** (Phase 5): `MENOSEstimator` for exact expectation values.
- **Bitwise-identical CUDA results** (Phase 10): kernels are built without fused multiply-add so they match the CPU exactly.
- **Budgets enforced while running** (Phase 11): an overrun is abandoned at a safe boundary and replanned. The budget bounds one attempt.

## Known deferrals

Recorded, not scheduled: checkpointing and state migration; per-fragment CPU/CUDA selection for cut plans; CUDA kernels specialised to diagonal or permutation gates; asynchronous CUDA gate launches; multi-GPU, ROCm and Metal; Windows; OpenQASM 3. See [Limitations](/reference/limitations).

## Design records

The reasoning behind each phase is preserved as architecture decision records in the repository's `docs/adr/` directory.
