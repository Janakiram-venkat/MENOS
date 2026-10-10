# Light-Cone Reduction

For a given output, only some operations can matter. MENOS removes the rest **before planning**, so every engine sees a smaller problem.

## The idea

An operation influences an output only if there is a chain of gates connecting it to the qubits the output depends on. Work backward from the output: the observable's qubits are in the cone; any gate that touches a qubit in the cone pulls its other qubits into the cone, but only for gates that come *earlier* in the circuit. Everything outside the cone cannot change the result and is dropped.

## What the output depends on

| Output | Qubits it depends on |
|---|---|
| Statevector | every qubit |
| Counts | every measured qubit |
| Expectation value | the union of each Pauli term's support |

For a statevector the cone is everything, so reduction is a lossless no-op. The pass still runs every time, and the report records it.

## A measured example

The same 12-qubit, 53-operation circuit (three layers of rotations and entanglers), different observables:

| Observable | `light_cone` line |
|---|---|
| `Z` on qubit 0 | `qubits 12->4 operations 53->13` |
| `Z` on qubits 0 and 11 | `qubits 12->8 operations 53->26` |
| `Z` on every qubit | `qubits 12->12 operations 53->53` |
| a statevector request | `qubits 12->12 operations 53->53` |

A local observable turns a 12-qubit problem into a 4-qubit one, and the planner then plans *that*.

## Why it matters

Reduction is applied once, per job, before any engine is consulted. It:

- shrinks dense memory and time exponentially (removing 8 qubits is a 256× smaller state);
- can make a circuit simulable by an engine that would otherwise refuse it;
- changes nothing about the answer, which is exact either way.

## Renumbering

Surviving qubits are renumbered. The observable is remapped to match, and the report's `region` lines show qubits in the **reduced** circuit's numbering. The `circuit` line reports the original circuit.

## Global phase

Reduction drops the circuit's global phase from the reduced circuit. That is sound because global phase cancels in counts and in expectation values, and a statevector request never reduces. The runtime applies the original circuit's phase where it matters.

## Reading it in a report

```text
circuit    qubits=12 clbits=0 operations=53 depth=… global_phase=0
light_cone qubits 12->4 operations 53->13
```

## Related

[Analyze](/execution/analyze) · [Expectation Values](/build/expectation-values) · [Pauli Observables](/observables/pauli-observables)
