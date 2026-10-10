# Common Errors

The mistakes people make first, with the messages you will see.

## `AttributeError: Option 'X' is not valid for this backend`

| | |
|---|---|
| **Symptom** | `backend.run(qc, time_budget_seconds=1.0)` raises `AttributeError` |
| **Cause** | `MENOSBackend` accepts only `shots`, `seed_simulator` and `want_statevector` |
| **Solution** | Use those three. Time and memory budgets are available through the [native API](/build/native-api) |

## `ValueError: MENOSBackend.run() does not support batching`

| | |
|---|---|
| **Symptom** | `backend.run([qc1, qc2])` fails |
| **Cause** | One circuit per call |
| **Solution** | Call `run` once per circuit |

## `TypeError: MENOSBackend.run() expects a QuantumCircuit`

| | |
|---|---|
| **Symptom** | You passed something other than a circuit |
| **Solution** | Pass a `qiskit.QuantumCircuit` |

## My counts keys have leading zeros

| | |
|---|---|
| **Symptom** | Keys like `001` when you expected `1` |
| **Cause** | Keys are as wide as the classical register, and unwritten bits are `0` |
| **Solution** | Declare a register of the width you want, for example `QuantumCircuit(3, 1)`. See [Counts](/build/counts) |

## My counts keys look like `0x3`

| | |
|---|---|
| **Symptom** | The native API returns `{'0x0': 507, '0x3': 493}` |
| **Cause** | The native API keys counts by hexadecimal string |
| **Solution** | Use `MENOSBackend` for Qiskit's binary keys, or convert: `format(int(key, 16), "b")` |

## Results differ between runs

| | |
|---|---|
| **Symptom** | Two runs of the same circuit give different counts |
| **Cause** | `MENOSBackend` draws a fresh seed when `seed_simulator` is `None` |
| **Solution** | Pass `seed_simulator`. See [Determinism](/concepts/determinism) |

## The statevector is very large

| | |
|---|---|
| **Symptom** | Slow or refused jobs with `want_statevector=True` |
| **Cause** | A statevector is `16 · 2ⁿ` bytes |
| **Solution** | Ask for counts or an expectation value when that is enough. See [Memory Errors](/troubleshooting/memory-errors) |

## `explain()` labels run into values

| | |
|---|---|
| **Symptom** | `candidate 0strategy=direct_dense …` |
| **Cause** | Labels are padded to 11 characters, so a label of 11 or more runs into its value. It is the shipped format |
| **Solution** | Nothing to fix. See [Plan Explanation](/planning/explain) |

## `InternalError`

| | |
|---|---|
| **Symptom** | `menos._core.InternalError: …` |
| **Cause** | A MENOS invariant was violated, or a case MENOS does not handle |
| **Solution** | Check [Exceptions](/reference/exceptions) for the known case (an all-identity observable) and the CUDA entries in [Troubleshooting CUDA](/gpu/troubleshooting-cuda). Otherwise it is a bug worth reporting |

## `ResourceExceededError` for a job that used to work

| | |
|---|---|
| **Symptom** | A job now raises `ResourceExceededError` mentioning an observed runtime |
| **Cause** | You set a time budget, the run overran it, and no other plan fits |
| **Solution** | Raise the budget, or remove it. See [Replanning](/runtime/replanning) |

## A replanned job took longer than its budget

| | |
|---|---|
| **Symptom** | Total wall time is a multiple of `time_budget_seconds` |
| **Cause** | The budget bounds **one attempt**; abandoned attempts add to the total |
| **Solution** | This is by design. See [Time Budgets](/runtime/time-budgets) |

## See also

[Installation Problems](/troubleshooting/installation) · [Unsupported Circuits](/troubleshooting/unsupported-circuits) · [Exceptions](/reference/exceptions)
