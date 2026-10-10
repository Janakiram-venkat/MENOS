# Noise Models

By default MENOS simulates noisy circuits **exactly**, using Qiskit Aer's `NoiseModel` as the way you describe the noise. Nothing is sampled except the shots you ask for, so expectation values are exact for the model, and the [density-matrix engine](/engines/density-matrix) that runs them is the same reference Aer's own `method="density_matrix"` provides.

Noise needs Qiskit Aer:

```bash
pip install "menos[qiskit]" qiskit-aer==0.17.2
```

## Counts with a noise model

Pass the model to `MENOSBackend.run`, exactly as you would to `AerSimulator`:

```python
from qiskit import QuantumCircuit
from qiskit_aer.noise import NoiseModel, ReadoutError, depolarizing_error

from menos.qiskit import MENOSBackend

noise_model = NoiseModel()
noise_model.add_all_qubit_quantum_error(depolarizing_error(0.05, 2), ["cx"])
noise_model.add_all_qubit_readout_error(ReadoutError([[0.97, 0.03], [0.05, 0.95]]))

qc = QuantumCircuit(2, 2)
qc.h(0)
qc.cx(0, 1)
qc.measure([0, 1], [0, 1])

counts = (
    MENOSBackend()
    .run(qc, shots=10_000, seed_simulator=1, noise_model=noise_model)
    .result()
    .get_counts()
)
print(counts)
assert sum(counts.values()) == 10_000
assert counts.get("01", 0) + counts.get("10", 0) > 0  # noise makes odd parity possible
```

`noise_model` can also be set once with `backend.set_options(noise_model=...)`.

## Exact expectation values

`MENOSEstimator` takes the same model:

```python
from qiskit import QuantumCircuit
from qiskit.quantum_info import SparsePauliOp
from qiskit_aer.noise import NoiseModel, depolarizing_error

from menos.qiskit import MENOSEstimator

noise_model = NoiseModel()
noise_model.add_all_qubit_quantum_error(depolarizing_error(0.3, 1), ["h"])

qc = QuantumCircuit(1)
qc.h(0)

result = MENOSEstimator(noise_model=noise_model).run([(qc, SparsePauliOp("X"))]).result()[0]
print(result.data.evs)
# Depolarizing with p = 0.3 shrinks <X> on |+> from 1 to 0.7, exactly.
assert abs(float(result.data.evs) - 0.7) < 1e-12
assert float(result.data.stds) == 0.0
```

The values are exact, so `stds` is zero. Readout errors are **not** applied to expectation values: they change recorded measurement outcomes only. Aer's own `EstimatorV2` does the same.

## Supported noise types

Phase 12 accepts the noise types that have been verified against Aer, one at a time:

| Noise type | Aer constructor | Accepted |
|---|---|---|
| Depolarizing, 1 or 2 qubits | `depolarizing_error(p, n)` | Yes |
| Amplitude damping | `amplitude_damping_error(gamma)` | Yes |
| Depolarizing and amplitude damping on one qubit, composed in either order | both added to one instruction, or `.compose()` | Yes |
| Readout error, one qubit | `ReadoutError([[p00, p01], [p10, p11]])` | Yes |
| Phase damping, thermal relaxation, Pauli, reset, generalized amplitude damping, tensor products of one-qubit errors | | Not yet: `UnsupportedFeatureError` |
| Correlated readout error on several qubits | | Not yet: `UnsupportedFeatureError` |

An unsupported type is refused with a message naming the instruction and qubits. It is never silently dropped or approximated. More types are added as each one is verified.

```python
from qiskit import QuantumCircuit
from qiskit_aer.noise import NoiseModel, phase_damping_error

from menos.exceptions import UnsupportedFeatureError
from menos.qiskit import MENOSBackend

noise_model = NoiseModel()
noise_model.add_all_qubit_quantum_error(phase_damping_error(0.1), ["h"])
qc = QuantumCircuit(1, 1)
qc.h(0)
qc.measure(0, 0)
try:
    MENOSBackend().run(qc, shots=10, noise_model=noise_model)
    raise AssertionError("expected a refusal")
except UnsupportedFeatureError as error:
    print(error)
    assert "verified noise types" in str(error)
```

## How a noise model is applied

MENOS follows `AerSimulator`:

- **Gate errors** are attached to the instructions of the circuit **as submitted**, by name and qubits, with Aer's own rules for which error applies where: a qubit-specific error overrides an all-qubit one. Transpile first if your noise model is written for a particular basis.
- **Errors on `measure`** act **before** the measurement, as in `AerSimulator`. (Aer's `insert_noise` helper writes them after the measurement, where they could not change the outcome; MENOS does not follow that.)
- **Errors on `id`** are applied, even though `id` itself does nothing. This is how idle noise is usually modelled.
- **Errors appended directly** to a circuit with `qc.append(error, qubits)` are accepted under the same rules.

## Size

Exact noisy simulation stores the full density matrix: `16 · 4ⁿ` bytes, the square of a statevector. With the default memory budget of half of physical memory, 16 GiB of RAM allows about 14 qubits. Beyond that the job is refused with `ResourceExceededError`; it is never approximated instead, unless you opt in to [trajectory simulation](#trajectory-simulation), which needs only `16 · 2ⁿ` bytes.

## The native API

Without Qiskit Aer, the native `Circuit` takes noise directly: any channel as its Kraus operators, and a readout error on a measurement as `[P(0|0), P(1|0), P(0|1), P(1|1)]`:

```python
import math

from menos import Circuit, Opcode, RunOptions, run

gamma = 0.25
circuit = Circuit(1, 1)
circuit.append(Opcode.X, [0])
circuit.append_channel(
    [0],
    [[1, 0, 0, math.sqrt(1 - gamma)], [0, math.sqrt(gamma), 0, 0]],  # amplitude damping
)
circuit.append_measure(0, 0, readout_error=[0.99, 0.01, 0.02, 0.98])

options = RunOptions()
options.shots = 10_000
options.seed = 3
result = run(circuit, options)
print(result.counts)
assert result.exact
assert "direct_density_matrix" in result.explain()
```

The native API accepts **any** valid channel. Its matrix must be trace preserving within `1e-12`, the same tolerance as a unitary's check. The supported-types table above applies to Aer noise models only.

## Trajectory simulation

Trajectory simulation reaches noisy circuits far beyond the density matrix's size limit, at the price of statistical error. Each trajectory is one pure-state run in which every channel is replaced by one of its Kraus operators, drawn with its exact probability. The average over trajectories is unbiased for every channel, and the result reports its standard error. It is **never** reported as exact.

It is used only when you opt in, and then only when the planner finds it cheaper than the density matrix, or when it is the only plan that fits:

| Output | How to opt in |
|---|---|
| Expectation value | `RunOptions.expectation_precision = eps`, or a precision `> 0` in `MENOSEstimator` with a noise model |
| Counts | `RunOptions.noise_method = NoiseMethod.Auto`, or `noise_method="auto"` on `MENOSBackend` |

Each requested output must be opted into. A job that also requests counts needs `Auto` as well as a precision.

**The trajectory count.** For counts, one trajectory per shot, so the counts are independent samples of the exact noisy distribution. For an expectation value of `O = c₀I + Σ cᵢPᵢ`, with `C = Σ|cᵢ|` over the non-identity terms, MENOS runs `N = max(2, ⌈C²/eps²⌉)` trajectories. This guarantees, before anything runs, that the true standard error is at most `eps`. The reported `expectation_standard_error` is the measured one, usually smaller. The bound is conservative, so the planner may still prefer the exact density matrix when it is cheaper.

```python
import math

from menos import Circuit, Opcode, RunOptions, run
from menos._core import Observable, PauliString

n = 10
circuit = Circuit(n, 0)
for q in range(n):
    circuit.append(Opcode.RY, [q], [0.1 * (q + 1)])
p = 0.1  # depolarizing: rho -> (1 - p) rho + p I/2
a, b = math.sqrt(1 - 3 * p / 4), math.sqrt(p / 4)
circuit.append_channel([0], [[a, 0, 0, a], [0, b, b, 0], [0, -1j * b, 1j * b, 0], [b, 0, 0, -b]])
for q in range(n - 1):
    circuit.append(Opcode.CX, [q, q + 1])

z_last = PauliString()
z_last.num_qubits, z_last.x_words, z_last.z_words = n, [0], [1 << (n - 1)]
observable = Observable()
observable.terms = [(z_last, 1.0)]

options = RunOptions()
options.shots = 0
options.seed = 7
options.expectation = observable
options.expectation_precision = 0.05  # C = 1, so N = 400 trajectories
result = run(circuit, options)
print(result.expectation_value, "+/-", result.expectation_standard_error)
assert not result.exact
assert result.expectation_standard_error is not None
assert "direct_trajectory" in result.explain()
```

`explain()` shows the trajectory count and how it was derived, and marks the result as statistical. The same seed gives the same result, including after a [replan](/runtime/replanning). Trajectories run on the CPU, and are not used inside a factorized plan.

## Related

[Density Matrix engine](/engines/density-matrix) · [Noise example](/examples/noise) · [Exactness](/concepts/exactness) · [Limitations](/reference/limitations)
