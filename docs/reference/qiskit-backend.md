# Qiskit Backend

`menos.qiskit.MENOSBackend`, a Qiskit `BackendV2`.

```python
from menos.qiskit import MENOSBackend
```

## Class

```text
class MENOSBackend(BackendV2)
MENOSBackend(provider=None, **fields)
```

`**fields` override the default [options](#options): `MENOSBackend(shots=2048, seed_simulator=7)` sets new defaults for every call. A field that is not a valid option raises `AttributeError`.

## Properties

| Property | Value |
|---|---|
| `name` | `"menos"` |
| `description` | `"MENOS dense-engine simulator"`. A fixed label; the backend uses every engine, not only dense |
| `backend_version` | The installed `menos` package version |
| `max_circuits` | `1` |
| `num_qubits` | `None`: no fixed size; memory is the limit |
| `target` | A `Target` advertising exactly the [supported gates](/reference/supported-gates) plus `measure` and `unitary` |
| `provider` | `None` (there is no provider class) |
| `options` | See below |

## Options

| Option | Default | Meaning |
|---|---|---|
| `shots` | `1024` | Number of samples |
| `seed_simulator` | `None` | Sampling seed; `None` draws a fresh one |
| `want_statevector` | `False` | Also return the exact statevector |
| `noise_model` | `None` | A `qiskit_aer.noise.NoiseModel`, simulated exactly by default. See [Noise Models](/build/noise-models) |
| `noise_method` | `"exact"` | `"exact"`: a noisy circuit runs on the exact density matrix. `"auto"`: the planner may also sample it with [trajectories](/build/noise-models#trajectory-simulation) when that is cheaper or the only plan that fits. Any other value raises `ValueError` |
| `time_budget_seconds` | `None` | Wall-clock seconds one execution attempt may take. `None`: no budget. A run that overruns is replanned; with no plan left, `run` raises `ResourceExceededError`. Abandoned attempts appear in `result.results[0].metadata["menos_replans"]`. Must be finite and > 0, otherwise `InvalidCircuitError`. See [Time Budgets](/runtime/time-budgets) |
| `backend` | `None` | `None`: the planner chooses CPU or CUDA. `"cpu"` or `"cuda"` forces one (see [Forcing a backend](/planning/backend-selection#forcing-a-backend)). `"cuda"` raises `UnsupportedFeatureError` when CUDA cannot be used, and never falls back to the CPU. Any other value raises `ValueError` |

`backend.set_options(shots=…)` changes defaults after construction.

## `run`

```text
MENOSBackend.run(run_input, **options) -> MENOSJob
```

| Parameter | Meaning |
|---|---|
| `run_input` | One `QuantumCircuit` (a one-element list is also accepted) |
| `**options` | Overrides for `shots`, `seed_simulator`, `want_statevector`, `noise_model`, `noise_method`, `time_budget_seconds`, `backend` for this call |

| Raises | When |
|---|---|
| `TypeError` | `run_input` is not a `QuantumCircuit`, or `noise_model` is not an Aer `NoiseModel` |
| `ValueError` | `run_input` is a list of more than one circuit, `noise_method` is not `"exact"` or `"auto"`, or `backend` is not `None`, `"cpu"` or `"cuda"` |
| `AttributeError` | An option name is not valid |
| `InvalidCircuitError` | Unbound parameters, nothing to produce, `shots > 0` with no measurement, malformed circuit, a `time_budget_seconds` that is not finite and > 0 |
| `UnsupportedFeatureError` | A gate or feature MENOS cannot represent, or `backend="cuda"` when this build has no CUDA support, no device is usable, or the circuit is noisy |
| `ResourceExceededError` | No plan fits the memory budget, or the time budget (at plan time, or after an overrun with no plan left) |

The call is synchronous and returns a finished [`MENOSJob`](/reference/job-api).

## Verification

```python
import inspect

from menos.qiskit import MENOSBackend

assert str(inspect.signature(MENOSBackend.__init__)) == "(self, provider=None, **fields) -> 'None'"
assert str(inspect.signature(MENOSBackend.run)) == "(self, run_input, **options) -> 'MENOSJob'"

backend = MENOSBackend(shots=64, seed_simulator=5)
assert dict(backend.options.items()) == {
    "shots": 64,
    "seed_simulator": 5,
    "want_statevector": False,
    "noise_model": None,
    "noise_method": "exact",
    "time_budget_seconds": None,
    "backend": None,
}
assert backend.name == "menos"
assert backend.max_circuits == 1
assert backend.num_qubits is None
assert backend.provider is None
assert backend.description == "MENOS dense-engine simulator"
```

## Related

[MENOSBackend guide](/build/menos-backend) · [Running Jobs](/build/running-jobs) · [Qiskit Integration](/get-started/qiskit-integration)
