# Trajectory

The trajectory engine simulates **noisy circuits by sampling**. Each trajectory is one pure-state run in which every noise channel is replaced by one of its Kraus operators, drawn with its exact probability. Averaging over many trajectories reproduces the noise exactly in expectation, so results are unbiased, but they are **statistical**: never reported as exact, and an expectation value comes with its measured standard error.

It needs `16 · 2ⁿ` bytes, a statevector's size, where the exact [density matrix](/engines/density-matrix) needs `16 · 4ⁿ`. That is what lets it reach noisy circuits far beyond the density matrix's roughly 14 qubits.

## Capabilities

| Output | Supported |
|---|---|
| Counts | Yes: one shot per trajectory, readout errors applied per shot as the density-matrix engine applies them |
| Expectation value | Yes: the mean over trajectories, with its standard error |
| Statevector | **No**: a noisy job has no single final state |
| Noise channels | Yes: any completely positive, trace-preserving channel on up to 5 qubits |
| Readout errors | Yes, single-qubit |
| CUDA | No, CPU only |

## When it is used

Only when you opt in, for each output you request:

| Output | Opt in with |
|---|---|
| Expectation value | `RunOptions.expectation_precision` (or a precision `> 0` in `MENOSEstimator` with a noise model) |
| Counts | `RunOptions.noise_method = NoiseMethod.Auto` (or `noise_method="auto"` on `MENOSBackend`) |

A job that does not opt in never sees this engine: its plan and report are exactly what they were before. A job that opts in for one output but also requests another lists the candidate as infeasible with the reason, for example `counts by trajectory sampling need noise_method=Auto`.

When the candidate is available it **competes with the density matrix on cost**. Trajectories win when the density matrix does not fit the memory budget, or when N trajectories of `2ⁿ` amplitudes cost less than one `4ⁿ` density matrix: roughly, when `N < 2ⁿ⁺¹`. A small circuit with a tight precision therefore still runs exactly. If a density-matrix run overruns its [time budget](/runtime/monitoring), the [replan](/runtime/replanning) can move it to trajectories.

## How it works

For each trajectory, ψ starts at |0…0⟩ and the circuit is applied in order:

- a gate is applied exactly, with the same kernel as the [dense engine](/engines/dense);
- a channel `{Kᵢ}` on k qubits computes the k-qubit reduced density matrix ρ_q of ψ in one pass, takes the exact branch probabilities `pᵢ = Tr(Kᵢ†Kᵢ ρ_q)`, draws one branch, applies its Kᵢ and renormalizes ψ by its measured norm.

This is correct for every channel, including amplitude damping, whose branch probabilities depend on the state.

## The trajectory count

```text
counts:       N = shots                          one shot per trajectory
expectation:  N = max(2, ⌈C²/eps²⌉)              C = Σ|cᵢ| over non-identity terms
both:         N = max(shots, ⌈C²/eps²⌉)
```

Every trajectory's value of `O = c₀I + Σ cᵢPᵢ` lies within C of c₀, so its variance is at most C², and this N **guarantees** before anything runs that the true standard error is at most `eps`. The reported standard error is the measured one, `s/√N`, usually well below `eps`.

The bound is deliberately conservative. Measured on noisy brickwork circuits with `O = Σ Zᵢ`, the real variance was 0.7% to 3% of C² for 4 to 12 qubits, so N was 33 to 140 times larger than that precision needed. That costs time, never correctness. Stopping adaptively once the error is small enough is not done, because the planner could not know the cost in advance and the stopping rule would bias the result.

## Cost

```text
work = N · ( 2ⁿ                                 reset
           + Σ gates 2^(n+k)                    as the dense engine
           + Σ channels (2 · 2^(n+k) + 2ⁿ)      reduced density matrix, apply Kᵢ, renormalize
           + terms · 2ⁿ )                       (expectation)
     + shots · 2ⁿ                               (counts) one distribution per shot
peak bytes = 16 · 2ⁿ  +  16 · 4ᵏ per Kraus operator of every channel  +  8 · 2ⁿ (counts)
```

The planner's seconds come from the CPU rate calibrated on the dense engine. Measured against forced runs, they were within 10% up to 12 qubits and optimistic at larger sizes: measured runs took 1.22 to 1.25 times the prediction at 16 qubits and 1.51 times at 20 (`docs/adr/0017` section 10).

## Determinism

Trajectory t uses its own random stream, seeded from the job seed by SplitMix64. So the same circuit, noise, options and seed give the same result, including after a replan, and a trajectory's result does not depend on how many ran before it.

## Related

[Noise Models](/build/noise-models#trajectory-simulation) · [Density Matrix](/engines/density-matrix) · [Exactness](/concepts/exactness) · [Dense](/engines/dense)
