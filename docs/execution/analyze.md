# Analyze

Analysis turns a validated circuit into the facts the planner needs. Three things happen, in this order.

## 1. Validation

Before anything else, the circuit is checked once:

- every qubit and classical-bit index is in range, and no qubit repeats within one operation;
- gate arity and parameter count match, and parameters are finite;
- a unitary acts on at most five qubits and really is unitary within tolerance;
- at least one qubit exists;
- measurements are **terminal**: no operation follows a measurement of the same qubit;
- the *output request* is meaningful: `shots > 0` needs a measurement, and at least one of counts, statevector or expectation must be requested.

A failure here raises `InvalidCircuitError` or `UnsupportedFeatureError` (see [Exceptions](/reference/exceptions)); nothing has been planned or run.

## 2. Light-cone reduction

Only operations in the causal past of the requested output can change it. MENOS computes the set of qubits the output depends on, and drops every operation that cannot influence them, then renumbers the surviving qubits.

| Output requested | Qubits the output depends on |
|---|---|
| a statevector | every qubit |
| counts | every measured qubit |
| an expectation value | the union of each observable term's support |

For a statevector the reduced circuit equals the original, so the pass is lossless. For a single-qubit observable on a large circuit it can remove most of it. See [Light-Cone Reduction](/observables/light-cone-reduction). The report's `light_cone` line shows the qubit and operation counts before and after.

## 3. Interaction graph and components

MENOS builds a graph with one node per qubit and an edge between two qubits whenever an operation touches both. Its **connected components** are groups of qubits that never interact.

- One component: a single region.
- Two or more: a `factorized` plan is enumerated, which runs each component on its own engine and recombines the results exactly.

Component and qubit order are fully determined by the circuit, so the same circuit always yields the same regions.

## 4. Cut search (expectation requests only)

For an expectation request MENOS also searches for a *wire cut*: a place where cutting one or two qubit wires splits the circuit into independent fragments. If one exists, a `cut_and_recombine` candidate is enumerated. The search is bounded (at most two cuts), so it cannot stall planning on a circuit that has no good cut. See [Cut and Recombine](/engines/cut-and-recombine).

## What analysis does not do

Analysis does not decide *how* to run anything. It produces facts; the [cost model](/planning/cost-model) and [planner](/execution/plan) make the decision.
