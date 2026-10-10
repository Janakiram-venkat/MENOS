# Estimator

`menos.qiskit.MENOSEstimator`, an implementation of Qiskit's `BaseEstimatorV2`.

```python
from menos.qiskit import MENOSEstimator
```

## Class

```text
class MENOSEstimator(BaseEstimatorV2)
MENOSEstimator(*, default_precision: float = 0.0, noise_model: NoiseModel | None = None,
               seed: int | None = None)
```

| Property | Meaning |
|---|---|
| `default_precision` | The precision used when `run` is not given one. Never adds noise |
| `noise_model` (constructor) | A `qiskit_aer.noise.NoiseModel`. With precision `0` expectation values are exact for that noise; with precision `> 0` the planner may [sample them with trajectories](/build/noise-models#trajectory-simulation). Readout errors do not apply to expectation values. See [Noise Models](/build/noise-models) |
| `seed` (constructor) | The trajectory-sampling seed. `None` draws a fresh seed for each `run()` call. The seed used is recorded in every result |

## `run`

```text
MENOSEstimator.run(pubs, *, precision=None) -> PrimitiveJob
```

`pubs` is an iterable of estimator pubs, each coercible by Qiskit's `EstimatorPub.coerce`: `(circuit, observables)` with optional parameter values. The job runs immediately and is already complete.

## Result

`job.result()` is a `PrimitiveResult` with one `PubResult` per pub. Each `PubResult` has:

| Field | Meaning |
|---|---|
| `data.evs` | `numpy` array of expectation values, shaped like the pub's observables |
| `data.stds` | The standard error of each value: the measured standard error when it was sampled by trajectories, otherwise `0` (the value is exact) |
| `metadata["target_precision"]` | The requested precision. Under a noise model, a precision `> 0` is the target standard error that lets the planner choose trajectories |
| `metadata["seed"]` | The seed used, so the run can be reproduced |
| `metadata["circuit_metadata"]` | The circuit's `metadata` |

`PrimitiveResult.metadata` is `{"version": 2}`.

## Raises

| Exception | When |
|---|---|
| `UnsupportedFeatureError` | The circuit has parameters. Bind them first with `assign_parameters` |
| `InvalidCircuitError`, `UnsupportedFeatureError`, `ResourceExceededError` | As for [`MENOSBackend.run`](/reference/qiskit-backend) |
| `InternalError` | Every term of an observable is the identity ([details](/observables/pauli-observables#an-all-identity-observable-is-not-supported)) |

## Verification

```python
import inspect

from qiskit.primitives import BaseEstimatorV2

from menos.qiskit import MENOSEstimator

assert issubclass(MENOSEstimator, BaseEstimatorV2)
assert str(inspect.signature(MENOSEstimator.__init__)) == (
    "(self, *, default_precision: 'float' = 0.0, noise_model=None, seed: 'int | None' = None) -> 'None'"
)
assert str(inspect.signature(MENOSEstimator.run)) == "(self, pubs, *, precision=None) -> 'PrimitiveJob'"
assert MENOSEstimator().default_precision == 0.0
assert MENOSEstimator(default_precision=0.1).default_precision == 0.1
```

## Related

[EstimatorV2 guide](/build/estimator) · [Expectation Values](/build/expectation-values)
