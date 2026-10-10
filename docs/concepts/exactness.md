# Exactness

MENOS **never silently approximates**. A result is exact, or it is flagged, or the job is refused.

## What "exact" means here

Exact means **no algorithmic approximation**: no truncation, no sampling of an expectation value, no discarded singular values. For a noisy job it means exact **for the noise model you asked for**: the noise is part of the question, and the density-matrix engine answers that question without approximating it. Arithmetic is still double precision, so results agree with Qiskit's reference simulators to about `1e-16`, not to infinite precision. The examples in this documentation check that agreement.

## Per engine

| Engine | Exactness |
|---|---|
| Dense | Exact |
| Stabilizer | Exact |
| Pauli propagation | Exact; refuses a job whose term count cannot be represented, rather than truncating |
| MPS | Exact by default: feasible only if a rigorous bound on the bond dimension fits the budget |
| Tensor network | Exact; contraction performs no truncation |
| Cut and recombine | Exact; every branch is enumerated, nothing is sampled |
| Factorized | Exact, as its regions are independent |
| Density matrix | Exact for the requested noise model; noise channels and readout errors are applied exactly, nothing is sampled except the requested shots |
| Trajectory | **Not exact**: noise is sampled. Used only when you opt in; the result is flagged `exact = False` and an expectation value carries its measured standard error. See [Trajectory](/engines/trajectory) |

## Where `Result.exact` comes from

`Result.exact` is the **post-execution** truth: it is `True` only if no approximation was actually made. A plan carries a pre-execution estimate of the same thing.

```python
from menos import Circuit, Opcode, RunOptions, run

circuit = Circuit(2, 2)
circuit.append(Opcode.H, [0])
circuit.append(Opcode.CX, [0, 1])
circuit.append_measure(0, 0)
circuit.append_measure(1, 1)

options = RunOptions()
options.shots = 10
options.seed = 1
result = run(circuit, options)
assert result.exact
assert "(exact)" in result.explain()
```

## Approximation exists in the C++ core, not in Python

The C++ core has an accuracy policy, `Controlled(tolerance)`, that lets the MPS engine discard singular values below a per-step cutoff. When it truncates, the result is marked approximate, and MENOS reports an a-posteriori **error bound** built from what was actually discarded (the tolerance bounds each step, not their sum, so the reported bound is the honest figure).

**None of that is reachable from Python.** There is no way to request it. The only non-exact results Python can produce are [trajectory](/engines/trajectory) results, which you request explicitly with `expectation_precision` or `NoiseMethod.Auto`, and which are flagged `exact = False`.

## Refusal instead of degradation

When an exact run does not fit the constraints, MENOS refuses. It does not fall back to a cheaper, less accurate method:

- an MPS run whose exact bound exceeds the memory budget is infeasible, not truncated;
- a cut plan refuses any non-exact policy;
- a run that overran its time budget and has no other plan throws `ResourceExceeded`.

## Related

[MPS](/engines/mps) · [Determinism](/concepts/determinism) · [ResourceExceeded](/runtime/resource-exceeded)
