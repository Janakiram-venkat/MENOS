# Density Matrix

The density-matrix engine simulates **noisy circuits exactly**. It evolves the full density matrix ρ, so noise channels and readout errors are applied with no sampling and no truncation, and results are exact for the noise model you asked for. It is the only engine that runs noise exactly; the [trajectory engine](/engines/trajectory) samples it instead, when you opt in.

## Capabilities

| Output | Supported |
|---|---|
| Counts | Yes, with readout errors applied per shot |
| Expectation value | Yes, exact: Tr(ρP) |
| Statevector | **No**: a mixed state has none |
| Noise channels | Yes: any completely positive, trace-preserving channel on up to 5 qubits |
| Readout errors | Yes, single-qubit |
| CUDA | No, CPU only |

## When it is used

The planner considers it **only for a job that contains noise**: a channel, or a measurement with a readout error. A noiseless job never sees it, and its plan and report are exactly what they were before noise support existed.

For a noisy job, every pure-state engine is still listed in the report, infeasible with the reason:

```text
operation 3 is a noise channel; this engine simulates pure states only
```

so you can see why each one lost. When the circuit splits into independent parts, a [factorized](/planning/execution-plans) plan can run only the noisy parts on this engine and the noise-free parts on cheaper ones.

Noise that acts only outside the [light cone](/observables/light-cone-reduction) of what you asked for cannot change the answer, because every channel is trace preserving. It is dropped, and such a job is planned as the noiseless job it is equivalent to. The report's `noise` line still shows that the job carried noise.

## How it works

ρ for n qubits is stored as a vector of 4ⁿ complex numbers, entry (row r, column c) at index `r + 2ⁿ·c`. A gate U is applied as U on the row qubits and its complex conjugate on the column qubits, which is ρ → UρU†. A channel with Kraus operators Kᵢ is applied as its superoperator Σ Kᵢ ⊗ K̄ᵢ on both at once, in place. Both use the same matrix kernel as the [dense engine](/engines/dense).

Counts are drawn from the diagonal of ρ. A readout error is applied after the true outcome is drawn, by one random draw per readout-errored bit. Expectation values are Tr(ρP) for each Pauli term, one pass over 2ⁿ entries. Readout errors change recorded counts only, never an expectation value, which matches Qiskit Aer.

## Cost

```text
work = 4ⁿ                                   initialization
     + Σ gates 2 · 2^(2n+k)                 a k-qubit gate: two kernel passes
     + Σ channels 2^(2n+2k)                 a k-qubit channel: one pass with 2k operands
     + 2ⁿ                                   (counts) the diagonal
     + terms · 2ⁿ                           (expectation) one pass per term
peak bytes = 16 · 4ⁿ  +  16 · 16ᵏ for the largest channel  +  8 · 2ⁿ (counts)
```

Memory grows as **4ⁿ**, the square of a statevector's 2ⁿ. With the default budget of half of physical memory, 16 GiB of RAM allows about 14 qubits. That reach is the price of exactness; larger noisy circuits need [trajectory sampling](/engines/trajectory), which you opt in to.

The planner's seconds for this engine come from the CPU rate calibrated on the dense engine. Measured against real runs, they were within a factor of about 1.8 and slightly optimistic at the largest sizes (see `docs/adr/0016`).

## Determinism

The same circuit, noise and seed give the same counts, including when readout errors are applied.

## Related

[Noise Models](/build/noise-models) · [Trajectory](/engines/trajectory) · [Exactness](/concepts/exactness) · [Noise example](/examples/noise) · [Dense](/engines/dense)
