# KIR

**KIR** (the Kernel Intermediate Representation) is the form every circuit takes inside MENOS. Whatever you submit, whether a Qiskit `QuantumCircuit`, a native `menos.Circuit`, or an OpenQASM file, is lowered to KIR first, and everything after that (analysis, planning, execution) sees only KIR.

## Why an intermediate representation

- **One input, many front ends.** The converter is the only place that knows about Qiskit. The planner and engines never do.
- **Validate once.** KIR is checked at the boundary, so nothing downstream re-checks it.
- **Engines stay simple.** An engine knows only matrices and operand lists, not where a circuit came from.

## What a circuit is

A KIR circuit has a fixed number of qubits, a fixed number of classical bits, a global phase, and a flat list of **operations** in program order. There are three kinds:

| Operation | Carries |
|---|---|
| **Gate** | An opcode, the qubits it acts on, and parameters |
| **Unitary** | The qubits it acts on and an explicit matrix (1 to 5 qubits) |
| **Measure** | A qubit and the classical bit it writes |

Nothing else exists. There is no control flow, no reset, no mid-circuit branching.

## The gate table

Each opcode has a fixed name, number of operands and number of parameters, and a flag saying whether it is **Clifford**. That single table is what the stabilizer engine consults to decide feasibility, and what the converter consults to decide whether a Qiskit gate is native. See [Supported Gates](/reference/supported-gates).

## Validation

A circuit is validated for index ranges, arity, finite parameters, unitarity, terminal measurements, and a meaningful output request. See [Analyze](/execution/analyze).

## Native access

The same representation is available directly from Python as `menos.Circuit`, with `append`, `append_unitary` and `append_measure`; see [Native API](/build/native-api).

## Related

[Architecture](/introduction/architecture) · [Conventions](/concepts/conventions)
