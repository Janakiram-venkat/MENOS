# Readout Mitigation

Readout errors make a measurement record the wrong bit. MENOS can undo them on average: it calibrates each measured bit's readout and inverts it. Mitigation is **explicit opt-in**. Nothing is mitigated unless you ask, and a mitigated result is never labelled exact.

## How it works

Beside your circuit MENOS runs two calibration circuits: one measures every measured qubit from `|0⟩`, the other after an `X` on each. From their counts it estimates, for each measured clbit `c`, `P(1|0)` and `P(0|1)`, and so the 2×2 readout matrix `A_c`. Your circuit's measured distribution `p` is then mapped to

```text
q = (A_1⁻¹ ⊗ … ⊗ A_m⁻¹) p
```

computed exactly over all `2ᵐ` outcomes. MENOS's readout errors are independent per measurement, so this per-bit (tensored) model is exact; only the matrices are estimated.

`q` is a **quasi-distribution**: it sums to 1, but entries can be slightly negative. It is reported as is, not projected onto a probability distribution, because projecting would bias it.

Each entry carries a **standard error** with two parts. The target's shot noise is computed exactly. The calibration's shot noise is computed by a first-order (delta-method) approximation. `gamma = ∏ ‖A_c⁻¹‖₁` is the overhead: for an observable with eigenvalues in `[−1, 1]`, an expectation computed from `q` has a shot-noise standard error of at most `gamma / √shots`.

The dense vectors take `40 · 2ᵐ` bytes and are checked against the memory budget before anything runs.

## With the SamplerV2

```python
from qiskit import QuantumCircuit
from qiskit_aer.noise import NoiseModel, ReadoutError

from menos.qiskit import MENOSSampler

model = NoiseModel()
model.add_all_qubit_readout_error(ReadoutError([[0.95, 0.05], [0.1, 0.9]]))

qc = QuantumCircuit(2, 2)
qc.h(0)
qc.cx(0, 1)
qc.measure([0, 1], [0, 1])

sampler = MENOSSampler(noise_model=model, seed=1, readout_mitigation=True)
result = sampler.run([qc], shots=20000).result()[0]
mitigation = result.metadata["readout_mitigation"]
print(mitigation["quasi_dists"], mitigation["gamma"])

assert mitigation["exact"] is False
assert abs(mitigation["quasi_dists"]["01"]) < 5 * mitigation["stds"]["01"] + 1e-3
```

`metadata["readout_mitigation"]` holds `quasi_dists` and `stds` (binary keys over all clbits, clbit 0 rightmost), `gamma`, `calibration_shots` (the pub's shot count), `calibration` (`P(1|0)` and `P(0|1)` per clbit) and `exact` (`False`). The calibration circuits go through the same noise model as your circuit, so their `X` gates carry gate error, as they would on hardware.

## With the native API

```python
from menos import Circuit, Opcode, ReadoutMitigationOptions, RunOptions, run_readout_mitigated

circuit = Circuit(1, 1)
circuit.append(Opcode.X, [0])
circuit.append_measure(0, 0, [0.9, 0.1, 0.2, 0.8])

options = RunOptions()
options.shots = 10000
options.seed = 3
mitigation = ReadoutMitigationOptions()
mitigation.calibration_shots = 10000

result = run_readout_mitigated(circuit, None, options, mitigation)
print(result.explain())
assert abs(result.quasi_probabilities["0x1"] - 1.0) < 5 * result.standard_errors["0x1"]
```

Passing `None` as the calibration uses noiseless preparation. Pass your own `(zero, one)` pair of circuits to calibrate under gate noise. They must measure exactly the target's measured `(qubit, clbit)` pairs.

## Refusals

- A circuit with no measurement, `shots = 0` or `calibration_shots = 0` raises `InvalidCircuitError`.
- An estimated readout no better than a coin flip (`P(1|0) + P(0|1) ≥ 1`) raises `UnsupportedFeatureError`, because it has no meaningful inverse.
- Mitigation vectors over the memory budget raise `ResourceExceededError` before any circuit runs.

Readout errors never change expectation values in MENOS, so `MENOSEstimator` needs no readout mitigation.
