# Noise

A Bell pair under a Qiskit Aer noise model: depolarizing errors on the gates, amplitude damping before measurement, and a readout error. MENOS computes the expectation values exactly, and the script checks them against Aer's own density-matrix estimator.

Needs `pip install "menos[qiskit]" qiskit-aer==0.17.2`.

<<< @/../examples/noise_density_matrix.py

## Output

```text
<ZZ>, <XX>, <ZI> = [0.95, 0.931, -0.0]
counts: {'00': 4828, '01': 733, '10': 776, '11': 3663}
```

Without noise, ⟨ZZ⟩ and ⟨XX⟩ would both be 1, and only `00` and `11` would ever be recorded. The two-qubit depolarizing error on `cx` scales both correlations by 0.95. ⟨XX⟩ is also scaled by the one-qubit error on `h`, 0.98, giving 0.931. Amplitude damping, which acts just before each measurement, pulls population toward `0`, so `00` outnumbers `11`. Damping one qubit of `11` records `01` or `10`, and readout error flips further bits, so about 15% of outcomes have odd parity. The expectation values are exact, which is why they can be compared with Aer to `1e-12`. Readout error is not part of them.

## See also

[Noise Models](/build/noise-models) · [Density Matrix](/engines/density-matrix)
