# Backend Selection

MENOS chooses the **algorithm** and the **hardware** separately.

## Two levels

1. **Algorithm, by work units.** The cheapest feasible candidate wins, exactly as described in [Plan](/execution/plan). This step never looks at timing.
2. **Backend, by calibrated seconds.** Only for the dense algorithm, the planner compares the estimated seconds of running on the CPU with running on CUDA, and picks the GPU only if it is **strictly faster**. Ties go to the CPU.

Dense is the only algorithm with a second backend, so it is the only one this step touches.

## What the CUDA estimate includes

The comparison uses *end-to-end* seconds: kernel launches, host-to-device transfers, the work itself, and device-to-host transfers. A kernel that is faster does not make the GPU faster overall if the transfers eat the gain. See [Cost Model](/planning/cost-model).

## When the GPU is not offered

The CUDA candidate exists only if **all** of these hold:

- the binary was built with CUDA support;
- a usable NVIDIA device is present *at run time* (checked then, not inferred from the build);
- the hardware profile is calibrated for CUDA (transfer rates, launch overhead and device work rate were all measured);
- the region's device memory fits the device budget.

If any is false, the dense region stays on the CPU, silently and correctly. An uncalibrated profile never selects the device, because comparing against a number that was never measured would be a guess.

## Device memory is a separate budget

The device budget comes from the free memory the driver reports. A state that fits comfortably in host memory can still be refused on the device, and the plan then runs on the CPU rather than failing. See [GPU Memory](/gpu/gpu-memory).

## Plan-level, not per gate

The backend is chosen **per dense region**, once. MENOS does not split a region's gates between CPU and GPU.

**Factorized plans** (since Phase 17). The regions of a `factorized` plan run one after another and each allocates its own state, so each **dense** region is decided on its own, by the same comparison as a direct dense plan: CUDA only if the region fits the device and is strictly faster, with ties going to the CPU. A region on another engine (stabilizer, MPS, Pauli) has no GPU backend and stays on the CPU. The plan's time estimate is the sum of each region's estimate on its chosen backend, so a time budget is judged against the backends that will actually run. Measured on factorized plans of dense blocks, letting the large regions run on the GPU was 1.31–1.63× faster than keeping every region on the CPU (`docs/adr/0021` section 10).

**Cut plans** keep every fragment on the CPU. A cut plan exists to make fragments small, and below 18 qubits one fragment evaluation was measured slower on the GPU (`docs/adr/0021` section 10).

## After an overrun

If a run overran its time budget on one backend, the observation steers the choice on replanning: each backend's estimate is raised to what was actually observed *before* the two are compared. A GPU that overran is therefore not chosen again, and the CPU, never having been tried, is considered on its own estimate. If both backends overran, the job is refused. See [Replanning](/runtime/replanning).

## Seeing the choice

The `region` line of the plan report names the backend:

```text
region 0   qubits=[0,1,…,19] engine=dense backend=cpu
```

The [CUDA example](/examples/cuda) prints which one was used.

## Forcing a backend

Since Phase 17 a caller can force the backend instead of letting the planner choose: `RunOptions.backend` (`menos.Backend.Cpu` or `menos.Backend.Cuda`), the `MENOSBackend` run option `backend="cpu"|"cuda"`, or `--backend cpu|cuda` on the command line. Unset is the default and changes nothing.

- **CPU** keeps every region on the CPU.
- **CUDA** keeps the planner's algorithm choice, but only among plans that can run on CUDA: direct dense simulation of a noiseless circuit, or a `factorized` plan whose regions are all dense and each fit the device (all of them then run on the GPU). Every other candidate is listed as infeasible with the reason, for example `backend=cuda requested: … has no CUDA backend` or `backend=cuda requested: region 0 runs on mps, which has no CUDA backend`.
- A CUDA request that cannot be honoured is **refused, never moved to the CPU**. No CUDA support in the build, no usable device, or a noisy circuit raises `UnsupportedFeatureError`. A state that does not fit the device, the memory budget or the time budget raises `ResourceExceededError`.
- A forced backend stays forced when a time-budget overrun replans the job, so an overrunning CUDA run is refused rather than steered to the CPU.
- The report says the backend was requested:

```text
backend    cuda (requested)
```

When both runs use dense simulation, statevectors and counts are bitwise identical on CPU and CUDA, and expectation values agree within `1e-12`. Forcing CUDA can, however, change the **algorithm**: a circuit the planner would have run on the stabilizer or MPS engine runs as dense simulation instead. The result is still exact, but counts are then a different sample of the same distribution for the same seed.

```python
import menos

circuit = menos.Circuit(2, 2)
circuit.append(menos.Opcode.H, [0])
circuit.append(menos.Opcode.CX, [0, 1])
circuit.append_measure(0, 0)
circuit.append_measure(1, 1)
options = menos.RunOptions()
options.shots = 100
options.seed = 1
options.backend = menos.Backend.Cpu
report = menos.run(circuit, options).explain()
print(next(line for line in report.splitlines() if line.startswith("backend")))
assert "backend    cpu (requested)" in report
```

## Related

[CPU vs CUDA](/gpu/cpu-vs-cuda) · [CUDA](/engines/cuda) · [Time Budgets](/runtime/time-budgets)
