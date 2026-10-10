# Large Statevector

A 20-qubit exact statevector: a million complex amplitudes, 16 MiB.

<<< @/../examples/large_statevector.py

## Output

```text
amplitudes: 1048576 | bytes: 16777216
```

The script checks that the state is normalized. On a CPU build this takes on the order of a second; on a CUDA-enabled build the planner moves it to the GPU (see [CPU vs CUDA](/gpu/cpu-vs-cuda)).

## Scaling

| Qubits | State size |
|---|---|
| 20 | 16 MiB |
| 25 | 512 MiB |
| 30 | 16 GiB |

Doubling the memory for each extra qubit is why dense simulation has a wall, and why [other engines](/engines/mps) exist. See [Memory Constraints](/planning/memory-constraints).
