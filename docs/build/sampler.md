# SamplerV2

`menos.qiskit.MENOSSampler` implements Qiskit's `BaseSamplerV2`. Given measured circuits it returns samples, one `BitArray` per classical register, in the layout of Qiskit's `StatevectorSampler`.

```text
MENOSSampler(*, default_shots: int = 1024, noise_model: NoiseModel | None = None,
             seed: int | None = None, readout_mitigation: bool = False)
MENOSSampler.run(pubs, *, shots=None) -> PrimitiveJob
```

```python
from qiskit import QuantumCircuit

from menos.qiskit import MENOSSampler

qc = QuantumCircuit(2, 2)
qc.h(0)
qc.cx(0, 1)
qc.measure([0, 1], [0, 1])

result = MENOSSampler(seed=7).run([qc], shots=1000).result()[0]
counts = result.data.c.get_counts()
print(counts)
assert set(counts) == {"00", "11"} and sum(counts.values()) == 1000
assert result.metadata["seed"] == 7
```

## Behavior

- **Seeds.** `seed=None` draws a fresh seed for each `run()` call. The seed used is recorded in every result's `metadata["seed"]`, so any run can be reproduced. Pub `j` runs under the `j`-th seed derived from it, the rule [`run_batch`](/reference/python-api#run-batch) uses.
- **Shot order.** MENOS produces counts, not a per-shot memory, so a `BitArray` lists its shots in ascending outcome order, not in sampling order. Counts, bitstrings as a multiset and expectation values are unaffected.
- **Noise.** `noise_model` is a `qiskit_aer.noise.NoiseModel`, applied as by [`MENOSBackend`](/build/noise-models), including readout errors.
- **Parameters.** Parameterized pubs are refused; bind parameters first.
- **Mitigation.** `readout_mitigation=True` adds a mitigated distribution to each result's metadata. The samples themselves are never changed. See [Readout Mitigation](/build/readout-mitigation).
