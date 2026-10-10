# Overview

MENOS is a quantum circuit simulation **compiler and runtime**. It takes a circuit, works out how it could be executed, estimates the cost of each option, and runs the cheapest one that fits your constraints.

## What it is

- **A simulator.** It produces measurement counts, exact statevectors, and exact expectation values of Pauli observables.
- **A compiler.** A circuit is lowered to an internal representation ([KIR](/concepts/kir)), validated, and reduced to the part that can influence the output you asked for ([light-cone reduction](/observables/light-cone-reduction)).
- **A planner.** A [cost model](/planning/cost-model) prices every feasible [execution plan](/planning/execution-plans) in a single shared unit, and the cheapest plan wins.
- **A runtime.** The chosen plan is executed, optionally [monitored](/runtime/monitoring) against a time budget and [replanned](/runtime/replanning) when a run overruns.
- **A multi-engine system.** Eight execution strategies sit behind one interface: [dense](/engines/dense), [stabilizer](/engines/stabilizer), [Pauli propagation](/engines/pauli), [matrix product state](/engines/mps), [tensor network](/engines/tensor-network), [cut and recombine](/engines/cut-and-recombine), [density matrix](/engines/density-matrix), and [trajectory](/engines/trajectory). The dense engine also runs on an [NVIDIA GPU](/engines/cuda).
- **An exact noisy simulator.** Qiskit Aer noise models are simulated exactly by the density-matrix engine, or, when you opt in, sampled by trajectories with a reported standard error. See [Noise Models](/build/noise-models).
- **A Qiskit integration.** `MENOSBackend` is a Qiskit `BackendV2`; `MENOSEstimator` is a `BaseEstimatorV2`.

## What it is not

Being precise about scope is part of the design, so this list is as important as the one above.

- It is **not a large-scale noisy simulator by default**. Noise is simulated exactly with a density matrix, which limits it to about 14 qubits on 16 GiB of RAM. [Trajectory sampling](/engines/trajectory) reaches statevector sizes, but only when you opt in, and its results are statistical. Only verified noise types are accepted. See [Noise Models](/build/noise-models).
- It does **not support mid-circuit measurement, reset, or classical control**. Measurements must be terminal. See [Unsupported Circuits](/troubleshooting/unsupported-circuits).
- It provides **no `SamplerV2` primitive**. Counts come from `MENOSBackend.run`.
- It is **not approximate unless you ask for approximation**. The only approximation the Python API can ask for is trajectory sampling of noise, which is flagged. See [Exactness](/concepts/exactness).
- It is **not a general quantum SDK**. It has no circuit-construction library of its own; you build circuits with Qiskit (or the small native `menos.Circuit` API) and MENOS executes them.

## A minimal tour

```python
from qiskit import QuantumCircuit
from menos.qiskit import MENOSBackend

qc = QuantumCircuit(2, 2)
qc.h(0)
qc.cx(0, 1)
qc.measure([0, 1], [0, 1])

counts = MENOSBackend().run(qc, shots=1000, seed_simulator=7).result().get_counts()
assert set(counts) <= {"00", "11"}
```

The result is an ordinary Qiskit `Result`. To see *how* MENOS ran it, use the native API, where every result carries a human-readable plan report:

```python
from menos import Circuit, Opcode, RunOptions, run

circuit = Circuit(2, 2)
circuit.append(Opcode.H, [0])
circuit.append(Opcode.CX, [0, 1])
circuit.append_measure(0, 0)
circuit.append_measure(1, 1)

options = RunOptions()
options.shots = 100
options.seed = 1
print(run(circuit, options).explain())
```

The report lists every candidate strategy, which won, and why. See [Plan Explanation](/planning/explain).

## Next

- [Why MENOS](/introduction/why-menos): the design motivation.
- [Architecture](/introduction/architecture): how the pieces fit together.
- [Installation](/get-started/installation): get it running.
