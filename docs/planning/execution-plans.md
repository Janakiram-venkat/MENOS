# Execution Plans

A plan is the complete, decided way to run one job. It is what `explain()` prints.

## Strategies

| Strategy | Regions | Runs |
|---|---|---|
| `direct_dense` | 1 | The dense engine on all qubits |
| `direct_stabilizer` | 1 | The stabilizer engine on all qubits |
| `direct_pauli` | 1 | Pauli propagation on all qubits (expectation) |
| `direct_mps` | 1 | The MPS engine on all qubits |
| `direct_tensor_network` | 1 | The tensor-network engine on all qubits (expectation) |
| `factorized` | one per component | Each group of non-interacting qubits on its own cheapest engine |
| `cut_and_recombine` | one per fragment | Fragments run separately, recombined exactly (expectation) |

## Regions

A **region** is a set of qubits executed together by one engine on one backend. It carries its qubits (as indices in the original circuit), the operations that act only inside it, its engine and backend, its estimated bytes and work units, and, for a factorized plan, *why* that engine was chosen over the alternatives.

```text
region 0   qubits=[0,1] engine=stabilizer backend=cpu why="stabilizer (pauli rejected: …)"
```

## Factorization

If the interaction graph has two or more connected components, MENOS enumerates a factorized candidate. Its cost is the sum of each region's cheapest feasible engine, and because each region is small it is often far cheaper than running everything together.

```text
candidate 0strategy=direct_dense feasible=true winner=false peak_bytes=256 work_units=224
candidate 1strategy=direct_stabilizer feasible=true winner=false peak_bytes=72 work_units=56
candidate 2strategy=direct_mps feasible=true winner=false peak_bytes=256 work_units=68
candidate 3strategy=factorized feasible=true winner=true peak_bytes=40 work_units=32
```

Here two independent Bell pairs are four qubits. Run together, the cheapest engine needs 56 work units. Run as two separate two-qubit regions they need 32 in total. Recombination is exact: the regions are independent, so their joint state is a tensor product, their joint counts are independent draws, and their joint expectation is a product of regional values.

## Exactness

A plan is *exact* unless it may approximate. Every plan reachable from Python is exact. The report's `strategy` line says `(exact)`.

## What a plan contains

| Part | Content |
|---|---|
| `strategy` | One of the above |
| `exact` | Whether any approximation is possible |
| `regions` | As above |
| `estimated` | Total state bytes and work units |
| `candidates` | **Every** enumerated candidate with feasibility, costs, winner flag and reason |
| `circuit` and `light_cone` | Summaries of the original circuit and of reduction |
| `time_budget` | Present only when the job set one |

The candidate list is what lets you see why the winner won and why each loser lost.

## Plans are data

A plan is deterministic and inspectable. It does not change during execution. If a run overruns a time budget it is **replaced** by a new plan, and the superseded plan is reported; see [Replanning](/runtime/replanning).

## Related

[Plan](/execution/plan) · [Plan Explanation](/planning/explain) · [Backend Selection](/planning/backend-selection)
