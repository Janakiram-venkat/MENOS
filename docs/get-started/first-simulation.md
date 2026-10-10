# Your First Simulation

The first circuit sampled measurement outcomes. This tutorial asks for more: the exact state, then an exact expectation value, and then how MENOS planned the job.

## 1. The exact statevector

Drop the measurements and ask for the statevector with `want_statevector=True`. Because nothing is sampled, `shots` is 0.

```python
import numpy as np
from qiskit import QuantumCircuit

from menos.qiskit import MENOSBackend

qc = QuantumCircuit(2)
qc.h(0)
qc.cx(0, 1)

result = MENOSBackend().run(qc, shots=0, want_statevector=True).result()
state = np.asarray(result.get_statevector())
print(state)

assert np.allclose(state, [2**-0.5, 0, 0, 2**-0.5])
```

The amplitudes are exact (no sampling noise). Only the [dense engine](/engines/dense) can produce a statevector, so this request is always planned onto it.

## 2. An expectation value

An expectation value such as `<ZZ>` can be computed without building the state at all. Use `MENOSEstimator` with a `SparsePauliOp`:

```python
from qiskit import QuantumCircuit
from qiskit.quantum_info import SparsePauliOp

from menos.qiskit import MENOSEstimator

qc = QuantumCircuit(2)
qc.h(0)
qc.cx(0, 1)

value = float(MENOSEstimator().run([(qc, SparsePauliOp("ZZ"))]).result()[0].data.evs)
print(value)
assert abs(value - 1.0) < 1e-12
```

For a Bell state `<ZZ>` is exactly 1. See [Expectation Values](/build/expectation-values).

## 3. How was it planned?

The Qiskit layer returns a standard Qiskit result and does not expose the plan. The native API does. Here is the same Bell circuit, measured, run natively:

```python
from menos import Circuit, Opcode, RunOptions, run

circuit = Circuit(2, 2)
circuit.append(Opcode.H, [0])
circuit.append(Opcode.CX, [0, 1])
circuit.append_measure(0, 0)
circuit.append_measure(1, 1)

options = RunOptions()
options.shots = 100
options.seed = 1

report = run(circuit, options).explain()
print(report)
assert "strategy" in report
```

The output looks like this:

```text
MENOS EXECUTION REPORT
circuit    qubits=2 clbits=2 operations=4 depth=3 global_phase=0
light_cone qubits 2->2 operations 4->4
candidate 0strategy=direct_dense feasible=true winner=false peak_bytes=64 work_units=32
candidate 1strategy=direct_stabilizer feasible=true winner=true peak_bytes=40 work_units=16
candidate 2strategy=direct_mps feasible=true winner=false peak_bytes=128 work_units=34
strategy   direct_stabilizer (exact)
region 0   qubits=[0,1] engine=stabilizer backend=cpu
estimated  state_bytes=40 work_units=16
actual     state_bytes=40 execute_s=0.000046 sample_s=0.000000
```

Three strategies were feasible. The stabilizer engine won because it had the fewest work units (16, against 32 for dense and 34 for MPS). The `actual` line is what was measured after running.

::: info A formatting detail
Labels are padded to 11 characters, so a label that is 11 characters or longer runs straight into its value: `candidate 0strategy=…`. That is the format as shipped, not a typo in this page. See [Plan Explanation](/planning/explain).
:::

## Where next

- [Plan Explanation](/planning/explain): read every line of that report.
- [Qiskit Integration](/get-started/qiskit-integration): what the backend supports.
- [Examples](/examples/): runnable programs for every engine.
