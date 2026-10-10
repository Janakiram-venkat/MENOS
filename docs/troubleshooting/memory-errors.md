# Memory Errors

Each entry is a **symptom**, its **cause**, and a **solution**.

## `ResourceExceededError`: direct dense simulation requires N bytes

| | |
|---|---|
| **Symptom** | `ResourceExceededError: plan_job: no feasible candidate -- direct dense simulation requires 17592186044416 bytes, budget is 7752519680 bytes; …` |
| **Cause** | You asked for a statevector (or the circuit is only feasible densely), and the state needs more memory than the budget. A dense state of `n` qubits needs `16 · 2ⁿ` bytes |
| **Solution** | Ask for less. Request counts or an expectation value instead, or use a smaller circuit, or split it. If the machine really has the memory, raise `memory_budget_bytes` ([native API](/build/native-api)) |

## The default budget is lower than my RAM

| | |
|---|---|
| **Symptom** | A job that "should fit" is refused |
| **Cause** | By default only **50%** of physical memory is budgeted |
| **Solution** | Set `RunOptions.memory_budget_bytes` explicitly through the native API. `MENOSBackend` has no such option |

## The message lists several reasons

| | |
|---|---|
| **Symptom** | A long error with several sections separated by semicolons |
| **Cause** | It names every candidate's refusal reason, so you can see what blocked each |
| **Solution** | Read each reason. A statevector request removes the stabilizer and MPS candidates ("only the dense engine can produce a statevector"), leaving only dense, which then fails on memory |

## A large job is refused but I only need an expectation value

| | |
|---|---|
| **Symptom** | A big circuit fails under a statevector request |
| **Cause** | A statevector is `2ⁿ` amplitudes regardless of what you do with it |
| **Solution** | Request an expectation value through `MENOSEstimator`. Pauli propagation, MPS, tensor-network and cut strategies avoid the full state. See [Expectation Values](/build/expectation-values) |

## The GPU ran out of memory

| | |
|---|---|
| **Symptom** | `cuda: cudaMalloc failed: out of memory` |
| **Cause** | Device memory was claimed after MENOS measured it |
| **Solution** | See [Troubleshooting CUDA](/gpu/troubleshooting-cuda) |

## The process itself runs out of memory

| | |
|---|---|
| **Symptom** | The operating system kills the process |
| **Cause** | Memory outside what MENOS budgets: for example large counts dictionaries, statevector copies in NumPy, or other processes. MENOS budgets engine state, not your program's other allocations |
| **Solution** | Request less, or lower the budget to leave headroom |

## See also

[Memory Constraints](/planning/memory-constraints) · [ResourceExceeded](/runtime/resource-exceeded) · [Requirements](/get-started/requirements)
