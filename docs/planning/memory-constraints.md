# Memory Constraints

Every plan has a memory budget, and the planner refuses any candidate that would exceed it **before** allocating anything.

## The default

Unless you set one, the budget is **50% of the machine's total physical memory**. Half is a deliberate choice: it leaves room for the operating system and for everything else the process holds.

## Setting it

The budget is `memory_budget_bytes` on the native API's `RunOptions`:

```python
from menos import Circuit, Opcode, RunOptions, run
from menos.exceptions import ResourceExceededError

circuit = Circuit(24, 0)
for q in range(24):
    circuit.append(Opcode.H, [q])
circuit.append(Opcode.CX, [0, 1])

options = RunOptions()
options.want_statevector = True
options.memory_budget_bytes = 64 * 1024 * 1024      # 64 MiB; a 24-qubit state needs 256 MiB

try:
    run(circuit, options)
except ResourceExceededError as error:
    print(str(error)[:120])
    assert "budget is 67108864 bytes" in str(error)
```

`MENOSBackend` does not expose it, so Qiskit users get the default.

## What is checked

The budget bounds each candidate's **estimated peak bytes**. For the dense engine that is `16 · 2ⁿ`. A candidate over the budget is infeasible with a reason that states both figures:

```text
direct dense simulation requires 17592186044416 bytes, budget is 7752519680 bytes
```

If every candidate is over the budget, planning throws `ResourceExceeded`, listing each reason, and no allocation has been attempted.

## Other engines are far smaller

The same budget lets very different problems through:

| Engine | Example | Memory |
|---|---|---|
| Dense | 20 qubits | 16 MiB |
| Dense | 30 qubits | 16 GiB |
| Stabilizer | 200 qubits | about 13 KB |
| MPS | 100 weakly entangled qubits | about 50 KB |

## Device memory

GPU memory is a separate budget; see [GPU Memory](/gpu/gpu-memory).

## Memory is exact, not approximate

Predicted peak bytes matched actual peak bytes in every workload measured, because each engine allocates what it estimates. `Result.actual_state_bytes` reports the real figure.

## Related

[Requirements](/get-started/requirements) · [Memory Errors](/troubleshooting/memory-errors) · [ResourceExceeded](/runtime/resource-exceeded)
