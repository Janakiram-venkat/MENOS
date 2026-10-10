# MENOSBackend

`menos.qiskit.MENOSBackend` is a Qiskit `BackendV2` that executes circuits through the MENOS runtime. It contains no simulation logic: it converts the circuit, calls the planner and runtime, and adapts the result.

```python
from menos.qiskit import MENOSBackend

backend = MENOSBackend()
print(backend.name, backend.max_circuits, dict(backend.options.items()))
assert backend.name == "menos"
assert backend.max_circuits == 1
assert dict(backend.options.items()) == {
    "shots": 1024,
    "seed_simulator": None,
    "want_statevector": False,
    "noise_model": None,
    "noise_method": "exact",
    "time_budget_seconds": None,
    "backend": None,
}
```

## Properties

| Property | Value |
|---|---|
| `name` | `"menos"` |
| `backend_version` | The installed `menos` package version |
| `max_circuits` | `1` |
| `target` | A `Target` with exactly the [supported gates](/reference/supported-gates), `measure` and `unitary`; `num_qubits` is `None` |
| `options` | `shots=1024`, `seed_simulator=None`, `want_statevector=False`, `noise_model=None`, `noise_method="exact"`, `time_budget_seconds=None`, `backend=None` |

`max_circuits` is 1 because a call runs exactly one circuit. Passing a list of more than one raises `ValueError` rather than silently running only the first.

## `run`

```text
MENOSBackend.run(run_input, **options) -> MENOSJob
```

`run_input` is a single `QuantumCircuit` (a one-element list is also accepted). Keyword options override the defaults for this call only.

| Option | Default | Meaning |
|---|---|---|
| `shots` | 1024 | Number of measurement samples. `0` means "no counts": you must then ask for something else. |
| `seed_simulator` | `None` | Seed for sampling. `None` draws a fresh non-deterministic seed. |
| `want_statevector` | `False` | Also return the exact statevector. |
| `noise_model` | `None` | A Qiskit Aer `NoiseModel`, simulated exactly. See [Noise Models](/build/noise-models). |
| `time_budget_seconds` | `None` | Wall-clock seconds one execution attempt may take. `None`: no budget. A run that overruns is replanned; with no plan left, `run` raises `ResourceExceededError`. Abandoned attempts appear in `result.results[0].metadata["menos_replans"]`. Must be finite and > 0, otherwise `InvalidCircuitError`. See [Time Budgets](/runtime/time-budgets) |
| `backend` | `None` | `None`: the planner chooses CPU or CUDA. `"cpu"` or `"cuda"` forces one (see [Forcing a backend](/planning/backend-selection#forcing-a-backend)). `"cuda"` raises `UnsupportedFeatureError` when CUDA cannot be used, and never falls back to the CPU. Any other value raises `ValueError` |

An option name that is not in that table raises `AttributeError`:

```python
from qiskit import QuantumCircuit

from menos.qiskit import MENOSBackend

qc = QuantumCircuit(1, 1)
qc.h(0)
qc.measure(0, 0)
try:
    MENOSBackend().run(qc, memory_budget_bytes=1 << 30)
except AttributeError as error:
    print(error)
    assert "not valid for this backend" in str(error)
```

A memory budget is not a backend option. It is available through the [native API](/build/native-api). A time budget is (`time_budget_seconds`, since Phase 17).

## Execution model

`run` is **synchronous**. The simulation has finished when it returns, and the returned [`MENOSJob`](/reference/job-api) already holds the result. No background thread is created.

## Errors

`run` raises the exceptions in [Exceptions](/reference/exceptions): `InvalidCircuitError`, `UnsupportedFeatureError` and `ResourceExceededError`, plus `TypeError` for a non-circuit argument and `ValueError` for a multi-circuit list.

## See also

- [Running Jobs](/build/running-jobs)
- [Results](/build/results)
- [Qiskit Backend reference](/reference/qiskit-backend)
