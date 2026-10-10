# Unsupported Circuits

MENOS refuses what it cannot represent rather than approximating it. Each entry is a **symptom**, its **cause**, and a **solution**; the messages are quoted from real runs.

## Measurement followed by an operation

| | |
|---|---|
| **Symptom** | `UnsupportedFeatureError: operation 1: qubit 0 has a non-terminal measurement (an operation follows a measurement of this qubit)` |
| **Cause** | Measurements must be terminal |
| **Solution** | Move the measurement to the end. Mid-circuit measurement is not supported |

## `reset`

| | |
|---|---|
| **Symptom** | `UnsupportedFeatureError: operation 1 (reset): not a MENOS-supported named gate and has no matrix representation; unsupported. Try transpile(qc, backend) first.` |
| **Cause** | `reset` is not a unitary |
| **Solution** | Restructure the circuit to avoid it |

## Classical control

| | |
|---|---|
| **Symptom** | `UnsupportedFeatureError: operation 2 (if_else): not a MENOS-supported named gate and has no matrix representation; unsupported.` |
| **Cause** | Conditional gates need mid-circuit classical feedback |
| **Solution** | Not supported. Use deferred measurement where the algorithm allows it |

## A gate on more than five qubits

| | |
|---|---|
| **Symptom** | `UnsupportedFeatureError: operation 0 (qft): acts on 6 qubits, exceeding kMaxUnitaryQubits=5, and is not a MENOS-supported named gate. Try transpile(qc, backend) first.` |
| **Cause** | Unitaries are limited to five qubits |
| **Solution** | `transpile(qc, backend)` lowers it into native gates |

## Unbound parameters

| | |
|---|---|
| **Symptom** | `InvalidCircuitError: operation 0 (rx): unbound parameter(s) [Parameter(t)]; bind all parameters before running (QuantumCircuit.assign_parameters)` |
| **Cause** | A circuit still contains `Parameter`s |
| **Solution** | `qc.assign_parameters({...})` before running |

## Parameterized circuits in the estimator

| | |
|---|---|
| **Symptom** | `UnsupportedFeatureError: MENOSEstimator does not currently support parameterized circuits …` |
| **Cause** | `MENOSEstimator` rejects any circuit with parameters, even when `parameter_values` are given in the pub |
| **Solution** | Bind first, as the [EstimatorV2 example](/examples/estimator) does |

## Shots without a measurement

| | |
|---|---|
| **Symptom** | `InvalidCircuitError: shots > 0 was requested but the circuit has no Measure operation` |
| **Cause** | There is nothing to sample |
| **Solution** | Add `measure`, or set `shots=0` and ask for a statevector |

## Nothing to produce

| | |
|---|---|
| **Symptom** | `InvalidCircuitError: request asks for neither counts (shots=0) nor a statevector nor an expectation; nothing to produce` |
| **Cause** | `shots=0` and `want_statevector=False` |
| **Solution** | Request an output |

## An unsupported observable

| | |
|---|---|
| **Symptom** | `InternalError: Tableau: num_qubits must be at least 1` from an expectation request |
| **Cause** | Every term of the observable was the identity, which MENOS does not currently handle |
| **Solution** | Add the constant yourself after the run. See [Pauli Observables](/observables/pauli-observables#an-all-identity-observable-is-not-supported) |

## An unsupported gate in one engine only

| | |
|---|---|
| **Symptom** | A candidate is listed as infeasible with `operation N (…) is not a Clifford gate` or `is not supported by Pauli propagation` |
| **Cause** | Not an error: that engine does not support the gate, and another engine ran the job |
| **Solution** | None needed. See [Execution Plans](/planning/execution-plans) |

## Silently dropped instructions

`barrier`, `id` and `delay` are dropped without error because they cannot change any output.

## See also

[Circuits and Gates](/build/circuits-and-gates) · [Supported Gates](/reference/supported-gates)
