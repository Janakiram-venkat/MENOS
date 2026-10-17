# PennyLane Device

MENOS is a PennyLane device, `menos.qubit`. A QNode on it runs each circuit through the MENOS planner, which picks the engine for every run. That covers variational algorithms and parameter-shift gradients too. Results agree with PennyLane's `default.qubit` within `1e-12`, global phase included.

The device needs PennyLane 0.45.1, which needs CPython 3.11 or later:

```bash
pip install "menos[pennylane]"
```

## A first QNode

<<< @/../examples/pennylane_device.py

The device is registered through PennyLane's `pennylane.plugins` entry point, so `qml.device("menos.qubit", ...)` finds it once `menos` is installed. The class is also importable as `menos.pennylane.MENOSQubitDevice`.

## Device options

```python no-run
qml.device(
    "menos.qubit",
    wires=None,                 # an int, an iterable of labels, or None (each circuit's own wires)
    seed=None,                  # int or None; see "Seeds" below
    time_budget_seconds=None,   # RunOptions.time_budget_seconds, for every run
    backend=None,               # None, "cpu" or "cuda"; a forced backend never falls back
    noise_method="exact",       # "exact" or "auto"; affects noisy circuits with shots only
)
```

Set shots on the QNode, as PennyLane recommends: `@qml.set_shots(1000)` or `qml.qnode(dev, shots=1000)`. The device's own `shots=` argument is deprecated by PennyLane and is accepted only for PennyLane's device test suite.

## Measurements

| Measurement | `shots=None` (analytic) | Finite shots |
|---|---|---|
| `expval`, `var` of a Pauli-sum observable | Exact | Estimated from samples |
| `probs` (wires, or a Pauli observable) | Exact; needs a dense plan | Estimated from samples |
| `state` | Exact, global phase included; needs a dense plan | Refused |
| `sample`, `counts` | Refused | Yes |

- **Analytic expectations** go through MENOS's observable path, so light-cone reduction, Pauli propagation, the stabilizer engine, MPS and tensor networks are all available to the planner. Each `expval` is one MENOS run. A `var` is ⟨O²⟩ − ⟨O⟩², with O² computed exactly, so it is two runs (none for an identity term, one for a single Pauli word).
- **An analytic result is always exact.** The device never asks for a statistical method without shots, and it refuses a result that MENOS reports as non-exact.
- **With shots,** PennyLane's own transforms rotate every measurement into one basis. Each shot bin is then one MENOS counts run, and every finite-shot measurement is computed from it, as `default.qubit` does. `sample()` rows are the counts in a seeded random order.

## Operations

Operations with a MENOS gate (`PauliX`, `Hadamard`, `S`, `T`, `SX` and their adjoints, `RX`, `RY`, `RZ`, `PhaseShift`, `U3`, `CNOT`, `CZ`, `SWAP`, `ControlledPhaseShift`, `Toffoli`) map to it directly. Any other operation with a matrix on at most 5 wires becomes one exact unitary. Anything else, such as a template, state preparation or a gate on more than 5 wires, is decomposed by PennyLane's own `decompose` transform, the same one `default.qubit` uses. `GlobalPhase`, `Identity` and `Barrier` are supported, and so is parameter broadcasting.

## Noise

Every PennyLane noise channel on at most 5 wires is simulated exactly through its own Kraus matrices. This includes `DepolarizingChannel`, `AmplitudeDamping`, `PhaseDamping`, `BitFlip`, `PauliError`, `ThermalRelaxationError` and `QubitChannel`. PennyLane's parameter conventions therefore hold as written: `DepolarizingChannel(p)` applies each Pauli with probability p/3.

- Analytic `expval` and `var` of a noisy circuit run on the exact [density-matrix engine](/engines/density-matrix) and agree with `default.mixed` within `1e-12`.
- Noisy counts run on the density matrix, or with `noise_method="auto"` also by [trajectories](/engines/trajectory) when the planner prefers them.
- Analytic `state` and `probs` of a noisy circuit are refused.
- A channel must be trace preserving within `1e-12`. That is stricter than PennyLane's own check, and MENOS refuses a channel outside it rather than renormalising it.

## Seeds

`seed=int` makes the device reproducible. One NumPy generator per device draws the seed of every run with shots, in the order the tapes and shot bins run, so the same seed and the same sequence of executions give identical results. `seed=None` seeds that generator from the operating system, as `default.qubit`'s `seed=None` does.

## Gradients

The device has no derivatives of its own. `diff_method="best"` and `"parameter-shift"` let PennyLane run the shifted circuits through the device, each planned on its own, and the gradients agree with `default.qubit`'s within `1e-12`. `"backprop"`, `"adjoint"` and `"device"` are refused (see below).

## What is refused

Each of these raises `menos.pennylane.MENOSDeviceUnsupportedError`, which is both `menos.UnsupportedFeatureError` and PennyLane's `DeviceError`, and names the construct:

- mid-circuit measurements, `qml.cond` and resets
- observables without a Pauli representation (`Hermitian`, `Projector`, `SparseHamiltonian`, and `Hadamard` used as an observable)
- `density_matrix`, `mutual_info`, `vn_entropy`, `purity`, classical shadows, and `state` with shots
- `Snapshot`
- `diff_method="backprop"`, `"adjoint"` and `"device"`
- program capture (`qml.capture`) and `qml.qjit` (Catalyst)
- an operation PennyLane cannot decompose into the supported set

## See also

[PennyLane example](/examples/pennylane) · [Noise Models](/build/noise-models) · [Determinism](/concepts/determinism) · [Exactness](/concepts/exactness)
