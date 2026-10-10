# CUDA

Which backend did the planner choose? This program works on a CPU-only build and a CUDA-enabled build alike.

<<< @/../examples/cuda.py

## Output

On a CPU-only build:

```text
region 0   qubits=[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19] engine=dense backend=cpu
execute: 1.084 s
```

On a CUDA-enabled build on a supported GPU, the same program reports `backend=cuda` and finishes in a fraction of the time (0.09 s on the development GPU). The two runs produce bit-identical statevectors. See [CPU vs CUDA](/gpu/cpu-vs-cuda) for the comparison and [CUDA Installation](/gpu/cuda-installation) to build with CUDA.

The script asserts the region ran on the dense engine and that the state is normalized, and accepts either backend.
