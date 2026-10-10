# Cut and Recombine

Circuit cutting lets MENOS simulate a circuit that is too large for any single engine by **splitting it into smaller fragments**, simulating each separately, and recombining the results **exactly**.

## Why it exists

Suppose two groups of qubits are densely connected internally but linked to each other through a single qubit. The whole circuit may not fit as a dense state, but each group does. Cutting the wire that carries the link turns one large problem into several small ones, at the price of running each fragment for several different *boundary conditions*.

## How it works: wire cutting

MENOS cuts **wires** (a qubit's timeline between two operations), not gates. Each cut wire is replaced by an exact decomposition into **eight terms**: an observable measured on one side and a state prepared on the other, with a coefficient. A plan with `K` cuts has `8^K` branches in principle, but fragments that share boundary conditions are evaluated once, so the number of *distinct* fragment runs is far smaller. For one cut the example below needs 10 distinct fragment evaluations, where running both fragments once for each of the 8 branches would need 16.

The expectation value is then reconstructed as a signed, weighted sum over the branches.

## Scope

| Aspect | What is implemented |
|---|---|
| Output | **Expectation values only**. Counts and statevector requests never enumerate cutting |
| Accuracy | **Exact**. Every branch is enumerated; nothing is sampled or approximated |
| Number of cuts | At most **2** |
| Fragments | Run on the cheapest engine for each fragment and boundary assignment |
| Backend | CPU |

Probability reconstruction and full-state reconstruction are **not implemented**. A cut plan refuses a non-exact accuracy policy outright.

## When the planner chooses it

Cutting is one candidate among the others and competes on work units. It is enumerated for an expectation request when a cut exists, and it wins only when it is genuinely cheaper. A circuit that has no good cut simply never lists it. In a four-qubit example where running the circuit directly is cheaper, the planner declines to cut.

## Overhead

Cutting trades memory for time. Each fragment needs far less memory than the whole circuit, but the number of fragment runs grows with the cuts (`8^K` branches, fewer distinct evaluations). The plan report shows it:

```text
cut        cuts=1 fragments=2 branches=8 distinct_evals=10 actual_peak_bytes=512
```

## Example

Two four-qubit clusters joined through one qubit. MENOS cuts the joining wire, and the result agrees with a dense statevector to about `1e-16`:

<<< @/../examples/cut_and_recombine.py

```text
strategy   cut_and_recombine (exact)
cut        cuts=1 fragments=2 branches=8 distinct_evals=10 actual_peak_bytes=512
expectation: 0.37247461285251077
agrees with the dense statevector to 1.1102230246251565e-16
```

## Limits

- Expectation values only.
- At most two cuts.
- Fragments are simulated on the CPU.
- A fragment's engine is chosen per assignment, so a plan's regions in the report show the fragments, not the final engine of every evaluation.

## Related

[Execution Plans](/planning/execution-plans) · [Expectation Values](/build/expectation-values) · [Analyze](/execution/analyze)
