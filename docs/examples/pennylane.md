# PennyLane

A three-qubit variational energy, its parameter-shift gradient and a sampled Bell pair, all on the PennyLane device `menos.qubit`. The script checks the energy and gradient against PennyLane's `default.qubit` to `1e-12`.

Needs `pip install "menos[pennylane]"` (CPython 3.11 or later).

<<< @/../examples/pennylane_device.py

## Output

```text
energy: 0.901186
gradient: [-0.000693 -0.386833  0.18495  -0.022353]
counts: {'00': 1008, '11': 992}
```

The energy and gradient are exact. Each parameter-shift circuit is a separate MENOS run, planned on its own. The counts come from one seeded run of 2000 shots, so `seed=7` gives the same split every time.

## See also

[PennyLane Device](/build/pennylane) · [Expectation Values](/build/expectation-values)
