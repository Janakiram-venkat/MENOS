# Architecture

MENOS is a pipeline. A circuit enters as a Qiskit `QuantumCircuit` and leaves as a Qiskit `Result`, and every stage in between is a separate component with one job.

```text
Qiskit QuantumCircuit
        ↓
   Converter (Python)            lowers gates to MENOS opcodes; rejects what cannot be represented
        ↓
       KIR                       the circuit in MENOS's own representation, validated
        ↓
  Light-cone reduction           drops operations outside the output's causal past
        ↓
     Analysis                    interaction graph, connected components, cut search
        ↓
    Cost model                   prices each candidate in work units; checks memory and time
        ↓
     Planner                     picks the cheapest feasible candidate, then the backend
        ↓
  Execution plan                 strategy, regions, engine and backend per region
        ↓
     Runtime                     runs each region, recombines, reports
        ↓
 ┌──────┼──────────────┬─────────────────┐
 ↓      ↓              ↓                 ↓
Dense  Dense/CUDA   Stabilizer/Pauli   MPS / Tensor network / Cut
        ↓
   Monitoring                    compares elapsed time with the time budget at safe boundaries
        ↓
    Replanning                   on an overrun: observe, replan with the existing planner, restart
```

## The components

**Converter.** `menos.qiskit.convert` turns a `QuantumCircuit` into the native circuit. Gates in the [supported table](/reference/supported-gates) map to opcodes; other gates with a matrix on up to five qubits become dense unitaries; anything else is rejected with an explicit error rather than approximated. `barrier`, `id` and `delay` are dropped because they do not affect the result.

**KIR.** The [Kernel Intermediate Representation](/concepts/kir) is a flat list of gate, unitary and measurement operations over numbered qubits and classical bits. It is validated once, at the boundary, so nothing downstream needs to re-check it.

**Analysis.** Builds the interaction graph of the circuit and its connected components, runs [light-cone reduction](/observables/light-cone-reduction), and searches for [wire cuts](/engines/cut-and-recombine).

**Cost model.** Each engine reports, for a given request, whether it can serve it and what it would cost: peak memory and *work units*. The unit is shared by all engines, which is what makes their costs comparable. Arithmetic is checked, so an unrepresentable estimate is reported as infeasible and never wraps around. See [Cost Model](/planning/cost-model).

**Planner.** Enumerates candidates in a fixed order, discards infeasible ones, picks the lowest work units (ties go to the earlier candidate), and then, for the dense algorithm only, chooses CPU or CUDA by calibrated wall-clock seconds. See [Backend Selection](/planning/backend-selection).

**Runtime.** Executes the plan. A multi-region plan runs its regions one at a time and recombines them exactly. See [How MENOS Executes](/execution/how-menos-executes).

**Engines.** Six strategies behind one interface. An *engine* is an algorithm; a *backend* is the hardware it runs on. Dense is the only algorithm with two backends.

**Monitoring and replanning.** When a job carries a time budget, engines report safe boundaries to a monitor. Crossing the budget abandons the attempt, records what the engine actually cost, and calls the same planner again. See [Runtime](/runtime/monitoring).

## Dependency direction

The C++ layers depend on one another in one direction only, and the build enforces it:

```text
api → runtime → engines → kernels
api → planner → cost → analysis → ir → common
```

Nothing lower knows about anything higher. In particular the runtime never plans and never interprets the time-budget monitor; both decisions belong to the layer above it.

## Where the Python package fits

```text
menos.qiskit     MENOSBackend, MENOSEstimator, MENOSJob, converter, target
menos            Circuit, Opcode, RunOptions, Result, run, exceptions
menos._core      the nanobind extension that wraps the C++ library (internal)
```

`menos.qiskit` contains no simulation logic. It converts, calls `menos.run`, and adapts the result.
