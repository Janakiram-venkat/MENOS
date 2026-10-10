# Your First Circuit

This tutorial builds a Bell state, runs it, and reads the result. It is the same program shown on the home page, explained line by line.

<<< @/../examples/bell_state.py

## What each step does

**Build the circuit.** `QuantumCircuit(2, 2)` has two qubits and two classical bits. `h(0)` puts qubit 0 in superposition, `cx(0, 1)` entangles it with qubit 1, and `measure` copies the qubits into the classical bits. Measurements must come last: MENOS does not support operations after a measurement of the same qubit.

**Create the backend.** `MENOSBackend()` needs no arguments. It is an ordinary Qiskit [`BackendV2`](/build/menos-backend).

**Run.** `backend.run(qc, shots=1000, seed_simulator=7)` executes the circuit. It runs **synchronously**: the work is finished when `run` returns, and `.result()` just hands back the answer.

**Read the counts.** `get_counts()` returns a Qiskit `Counts` object, a dictionary from bitstring to number of shots. Keys are written with the highest classical bit on the left, as everywhere in Qiskit.

## Why a seed?

`seed_simulator=7` makes the run reproducible: the same circuit, shots and seed give the same counts every time. Leave it out and MENOS draws a fresh seed. See [Determinism](/concepts/determinism).

## What if I want the exact state, not samples?

That is the next tutorial: [Your First Simulation](/get-started/first-simulation).

## Common first mistakes

| You wrote | Result |
|---|---|
| `backend.run(qc)` with no measurements | `InvalidCircuitError`: shots were requested but the circuit has no `Measure` |
| `backend.run(qc, shots=0)` | `InvalidCircuitError`: nothing was requested to produce |
| `backend.run([qc1, qc2])` | `ValueError`: one circuit per call |
| A gate after a `measure` on that qubit | `UnsupportedFeatureError`: non-terminal measurement |

The messages are quoted in [Common Errors](/troubleshooting/common-errors).
