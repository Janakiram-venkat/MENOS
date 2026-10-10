# ADR 0015: Phase 11 — runtime monitoring and replanning

## Status

**Implemented and verified. Phase 11 implementation is complete.** The final verification gate ran on `322eca5`: `scripts/verify.sh` exited 0 (`VERIFY_EXIT=0`, `verify.sh: all stages passed`) with CPU dev/release 496/496, CUDA dev/release 528/528 and Python 74 passed, 1 deselected. This ADR records the design, the measurements and their consequences; it does not record the owner's gate decision. (An earlier revision of this line, written before the final gate had run, said the gate had not yet been confirmed.)

Three kinds of statement are kept visibly separate, as in `docs/adr/0014`:

- **[MEASURED]** — a number produced by a command run on this machine.
- **[MENOS]** — a design decision made here.
- **[REJECTED]** — a design that was tried or measured and abandoned, with why.

Every number below was produced this session on the development machine
(release build, GCC 15.2, 16 cores, `work_units_per_second` ≈ 2.5e8).

---

## 1. The problem, measured

Phase 10 made `time_budget_seconds` a **strict** constraint: a candidate whose
estimated runtime exceeds it is infeasible, and if none fits, planning throws.
But it constrained an *estimate*. The first question of this phase was whether
that estimate is any good.

Predicted is the winning candidate's `estimated_seconds`; actual is
`execute_seconds + sample_seconds` **[MEASURED]**:

| workload | strategy | predicted (s) | actual (s) | actual/predicted |
|---|---|---|---|---|
| dense n=12 L=8 | direct_mps | 0.000939 | 0.039912 | **42.5** |
| dense n=16 L=8 | direct_mps | 0.001273 | 0.705576 | **554** |
| dense n=20 L=8 | direct_mps | 0.001607 | 1.365502 | **850** |
| dense n=22 L=8 | direct_mps | 0.001774 | 1.886012 | **1063** |
| dense n=24 L=8 | direct_mps | 0.001941 | 2.265370 | **1167** |
| dense n=16 L=40 | direct_dense | 0.668012 | 0.512161 | 0.77 |
| dense n=20 L=40 | direct_dense | 13.358160 | 11.223907 | 0.84 |
| factorized 4×6 L=8 | factorized | 0.000198 | 0.000263 | 1.33 |
| factorized 8×8 L=12 | factorized | 0.003145 | 0.002197 | 0.70 |
| clifford n=64 L=20 | direct_stabilizer | 0.000519 | 0.000827 | 1.59 |
| clifford n=200 L=40 | direct_stabilizer | 0.010810 | 0.015403 | 1.42 |
| statevector n=18 L=8 | direct_dense | 0.602828 | 0.482771 | 0.80 |
| statevector n=22 L=8 | direct_dense | 11.781213 | 10.375023 | 0.88 |

The deviation is **repeatable to under 1%** across five runs (n=20 counts:
1.3685 / 1.3785 / 1.3703 / 1.3689 / 1.3683 s), so it is a property of the cost
model, not of noise.

**The dense, stabilizer and factorized models are sound (0.70×–1.59×). The MPS
model is wrong by 42×–1167×.** A job with a 0.2 s budget was admitted and then
ran for 0.71 s, silently. That is the defect this phase fixes.

### 1.1 What is NOT worth monitoring

`Plan::estimated_state_bytes` versus `Result::actual_state_bytes()` was
**exactly equal in all thirteen workloads** **[MEASURED]**, because each engine
allocates precisely what it estimated. There is no memory deviation to detect,
and inventing a memory threshold would be a metric validating its own
arithmetic, which CLAUDE.md forbids. `estimated_work_units` is a synthetic unit
and is never measured directly.

**Time is the only prediction that deviates, so time is the only thing this
phase monitors.** **[MENOS]**

---

## 2. Threshold: elapsed exceeds the budget **[MENOS]**

> A replan is triggered when observed elapsed wall-clock time within an
> execution attempt exceeds that job's `time_budget_seconds`, tested at a safe
> execution boundary.

**No numeric threshold is invented anywhere in this phase.** The threshold is
the caller's own constraint. That is not a stylistic preference; the two
alternatives both require a constant and both are worse.

### 2.1 Projection from partial progress **[REJECTED — measured]**

The natural design is to project a finish time from progress so far and fire
when the projection exceeds the budget. Measured by running prefixes of the same
circuit through the same engine and projecting with a flat per-work-unit rate:

| | projection error at 10% / 30% / 50% of work |
|---|---|
| DENSE n=20 L=8 | 1.10× / 1.00× / 1.01× |
| DENSE n=22 L=8 | 1.11× / 1.02× / 1.00× |
| MPS n=16 L=8 | **0.40× / 0.24× / 0.24×** |
| MPS n=24 L=8 | **0.22× / 0.16× / 0.16×** |

MPS's seconds-per-work-unit swings **30× within a single run** (6.5e-8 → 2.0e-6)
as its bond dimension grows, so early work is far cheaper than late work. A
projected threshold would *under*-project by 4×–6× and **fail to fire on the one
workload whose prediction is worst**. Elapsed-versus-budget extrapolates nothing
and has no such failure mode.

### 2.2 A factor K on the prediction **[REJECTED]**

Firing when elapsed exceeds K × the predicted time for work already completed is
also sound, and fires earlier (MPS shows 24× error at 10% of work while dense
stays at 0.97×). It was rejected for two reasons: it needs new per-engine work
accounting inside `execute()` in all six engines, and K is an invented constant.
The measured separation would have supported K ≈ 4 (dense ≤ 1.11×, stabilizer
≈ 1.6×, MPS ≥ 24×), but calibration itself varies **~1.5× between processes**
(observed: `work_units_per_second` 2.5e8 in one process and ~3.5e8 in another,
swinging the same prediction from 11.78 s to 7.60 s), which would have eaten a
large part of that margin.

### 2.3 The budget bounds ONE attempt, not their sum **[MENOS]**

This is the one place the reading of `time_budget_seconds` had to be made
precise, and it was made precise because the obvious alternative was implemented
first and does not work.

With a single job-global clock, crossing the threshold means the budget is
**already spent**, so every replanned attempt re-crosses at its first boundary,
every engine acquires a floor above the budget, and the job is always refused.
The feature would collapse into "detect and refuse" and could never demonstrate
a correct restart at all.

So the budget bounds one execution attempt. The consequence is stated rather
than hidden: **a job that replanned took longer in total than its budget**, and
the abandoned attempt's seconds are reported in `Result::replans()` instead of
being folded into `Result::execute_seconds()`, which times only the attempt that
succeeded. Planning is not charged against the budget (bounded; one `plan_job` call was
measured at 0.05 ms to 17 ms mean, with an 85 ms worst single sample -- see §10).

Termination does not depend on the clock — see §5.

---

### 2.4 The semantics, stated without ambiguity **[MENOS]**

`time_budget_seconds` means exactly this, and nothing looser:

| | |
|---|---|
| **What it bounds** | the wall-clock duration of **one execution attempt** |
| **What it does not bound** | the **total** wall time of a job that replans |
| Where it applies | plan time (estimate) *and* run time (observation) |
| Planning / replanning | **not charged** against it |
| Abandoned attempts | reported in `Result::replans()` and `explain()`; **not** included in `Result::execute_seconds()`, which times only the attempt that succeeded |
| Unset | no monitor is installed; behaviour and `explain()` are unchanged |

**Worst case, so nobody has to derive it.** Each replan permanently excludes one
engine (§5), so a job can be abandoned at most once per feasible single-engine
candidate. Each abandoned attempt runs for at most its budget plus one boundary's
worth of work. Total wall time is therefore bounded by

> (number of attempts) × (budget + one boundary) + (planning time),

where the number of attempts is at most the number of feasible `Direct*`
candidates plus one. On the development machine's Clifford counts job that is
three engines (stabilizer, MPS, dense). **A job that replans can take several
times its budget in total, and that is by design, not a defect.** A caller who
needs a hard bound on total wall time must not rely on this option for it.

This is stated identically in three places that must not drift apart:
`include/menos/menos.h` (`RunOptions::time_budget_seconds`, the public header),
`src/plan/job_spec.h`, and `include/menos/result.h` (`ReplanEvent`).

The per-attempt reading is deliberately **not** being changed in this phase. The
alternative -- one clock across the whole job -- was implemented first and does
not work (§2.3).

## 3. Safe execution boundaries **[MENOS]**

| Site | Boundary | Why it is safe |
|---|---|---|
| dense, stabilizer, MPS, pauli, cuda_dense `execute()` | between operations | between gates the state is a complete, valid quantum state; nothing is handed to the caller; the engine's allocation is released by its own destructor during unwinding |
| tensor_network `execute()` | between observable terms | its `execute()` is a contraction per term, not a sweep; no point inside a contraction means anything |
| MPS / stabilizer sole-region counts | between shots | the partial histogram is simply discarded |
| `combine_counts` | between shots | multi-region counts; nothing is committed until it returns |
| `execute_factorized` / `execute_cut` | between regions / fragment assignments | already independent units |

**Not a boundary:** inside one `apply_matrix`, one contraction, or the dense
cumulative sampling pass.

### 3.1 Why sampling had to be monitored too

Monitoring only the gate sweep was implemented first and **failed the first
real-hardware test**: an MPS run overran a 2.37 s budget, took 5.56 s, and never
fired. Sampling is **69% of an MPS run's wall time** — 0.485 s of sampling
against 0.220 s of gate sweep at n=16 L=8 **[MEASURED]** — because that sampler
walks the whole chain per shot. A budget that did not bound the majority of the
work would not have been a budget.

The dense sampler is deliberately **not** monitored: it is a single O(2^n)
cumulative pass, the same order as one gate, so it cannot overrun by more than a
single gate's granularity, and a per-basis-state check would cost 16 million
clock reads at 24 qubits.

### 3.2 Why abandoning throws rather than returning

A stop-and-return protocol would have to be threaded through every engine's
inner loop and checked by hand at each level; one missed check would silently
ignore the request. Throwing cannot be silently ignored.

`ReplanRequested` derives from `std::exception` and **not** from
`common::Error`. That is load-bearing: `src/` contains **nine** pre-existing
`catch` sites and every one of them catches `common::ResourceExceeded`
specifically, while the CLI catches `common::Error`. There is no `catch(...)`
anywhere in `src/` or `include/` — verified this session. A type outside that
hierarchy therefore reaches the replan loop untouched, and a test asserts the
signal escapes all five CPU engines.

(Most of those nine sit in `estimate()` paths, but not all:
`analysis::degree_exceeds_budget` is reached from the tensor-network engine's
`execute()` as well. The property relied on is that none of them is a
catch-all, not where they sit.)

### 3.3 Cost of the seam **[MEASURED]**

`steady_clock::now()` costs **31.4 ns** (2,000,000 reads in 0.0628 s).

The worst case for the seam is the engine with the cheapest work per boundary.
That is the stabilizer engine at a large qubit count: `clifford(200, 40)` plus
1024 shots is 12,180 gate/measure operations and 1024 sampled shots, i.e.
**13,204 boundaries** in a ~17.0 ms run, or **1.29 µs of work per boundary**.
One clock read against that is an arithmetic ceiling of **2.4%**.

The direct A/B comparison **cannot resolve a difference that small on this
machine**, and it is reported that way rather than dressed up. Three runs of
`benchmarks/monitor_overhead_benchmark.cpp`, stabilizer arm, monitored versus
unmonitored: **+3.1%, +0.6%, −1.2%**. With ten repetitions the two arms came out
at 17.1 ms ± 0.30 and 16.9 ms ± 0.23, and with twelve at 16.6 ms ± 0.93 and
17.1 ms ± 0.20 — the run-to-run shift in either arm is larger than the effect
being looked for. Google Benchmark warns about CPU frequency scaling and ASLR on
this laptop, and the unmonitored arm's coefficient of variation reached 5.6%.

Dense (n=18) and MPS (n=16), whose per-boundary work is thousands of times
larger, showed no measurable difference in any run, as expected.

**The claim this phase makes is therefore the ceiling, not a point estimate:
the seam costs at most ~2.4% on the cheapest engine in the codebase, and is
not resolvable above noise there.** An earlier draft of this ADR quoted "+3.1%"
as the measured overhead; repeating the measurement showed that figure was a
single noisy sample, and it has been withdrawn.

**A job that sets no time budget installs no monitor and pays none of this** —
one predictable, perfectly-predicted branch on a null pointer per boundary. That
is every job written before Phase 11.

---

### 3.4 Abandoning the CUDA engine **[MEASURED]**

The CUDA dense engine calls the same seam (`cuda_dense_engine.cpp`, once per
operation), and it is the one engine where abandoning has a resource consequence
beyond host memory: a live `cudaMalloc` allocation that must be released while
the stack unwinds. `tests/api/cuda_execution_monitor_test.cpp` asserts that on
the real GPU rather than trusting that `DeviceBuffer` is RAII:

| Test | What it proves |
|---|---|
| `TheCudaEngineCallsTheMonitorOncePerOperation` | one boundary per operation on the device path |
| `ANullMonitorLeavesTheCudaStatevectorBitwiseIdentical` | a monitor that never abandons perturbs no bit of the result |
| `AThrowingMonitorAbandonsTheCudaEngineAtTheRequestedBoundary` | stops **at** boundary 4, not after; `calls() == 4` |
| `AnAbandonedCudaRunLeavesTheEngineProducingCorrectResults` | after 5 abandons the next run matches the CPU **bitwise** (kernels keep matrices in constant memory, so stale state would show as a wrong amplitude) |
| `TheMemoryProbeCanSeeALiveDeviceAllocation` | **control**: a live 256 MiB buffer visibly lowers free memory, so the probe below is not vacuous |
| `AbandoningReleasesTheDeviceAllocation` | fresh `cudaMemGetInfo` before and after 8 abandons of a 128 MiB state; free memory is restored to within 32 MiB |
| `TenAbandonsOfAnOversubscribingTotalNeverExhaustTheDevice` | ten abandons of a 512 MiB state (5 GiB total against ~3.6 GiB usable) cannot succeed if any leaks; only the abandon signal may escape |

Measured against a simulated leak (a scratch program that never frees the
buffer, no implementation touched): eight leaked 128 MiB buffers dropped free
device memory by **1024 MiB** against the test's 32 MiB tolerance, and leaking
512 MiB per abandon failed with `cudaMalloc failed: out of memory` on the **8th**
allocation. Both release tests would have failed.

A boundary is safe on the device because `kernels::cuda::apply_matrix`
synchronizes before returning: when the monitor runs, no kernel is in flight.

## 4. Observations: a floor, not a rate **[MENOS]**

`plan::RuntimeObservations` carries one kind of fact: engine E on backend B, on
this job, has already spent this many seconds. A **lower bound**, directly
observed.

The planner applies `max(model estimate, observed floor)`, so an observation can
only ever make a candidate look **more expensive**. A stale or understated
observation cannot make the planner optimistic.

A per-engine *rate* (observed seconds ÷ completed work units) was rejected for
the same measured reason as §2.1: a rate taken from partial MPS progress is
wrong by 4×–6×.

**Scope.** A floor is attributed to one `(engine, backend)` pair, so it applies
to single-engine `Direct*` candidates only. A `Factorized` or `CutAndRecombine`
candidate spans several engines; attributing the whole elapsed time to any one of
them would be an over-claim, and the floor's soundness is what everything else
rests on. Those plans therefore yield no observation, the replan reproduces the
same plan, and the job is refused with a message saying exactly that. Both models
measured accurate to 0.70×–1.33×, so this is not where the problem is.

### 4.1 Why this produces a better plan, and why a naive replan would not **[MEASURED]**

Running each engine directly on the same circuit:

| workload | MPS (s) | dense (s) | MPS/dense |
|---|---|---|---|
| dense n=16 L=8 | 0.708991 | 0.105540 | **6.72 — replan wins** |
| dense n=20 L=8 | 1.368052 | 2.317721 | 0.59 — replan would lose |
| dense n=22 L=8 | 1.823625 | 10.642053 | 0.17 — replan would lose |
| dense n=24 L=8 | 2.299954 | 47.916997 | **0.05 — replan would lose 21×** |

A naive "abandon and take the runner-up" would be a **21× regression at n=24**,
where MPS is genuinely the right engine and only its *prediction* is wrong. The
observed floor is what prevents that: at n=24 with a 3 s budget the threshold is
never crossed at all, and with a 1 s budget neither engine meets the budget and
the job is correctly refused.

Restart economics at n=16: break-even is `0.709 = f·0.709 + 0.017 + 0.106`, so
**f = 0.83** — restarting pays as long as the crossing is detected before 83% of
the run. (0.017 s is the measured mean replan cost of the n=20 counts job, used
here as a deliberately conservative figure; the n=16 job's own replan cost was
0.237 ms, which gives f = 0.85.) That is why boundaries had to be inside the engines: a `Direct*` plan
has exactly one engine invocation, so a runtime-level region boundary would have
detected nothing.

---

### 4.2 An observed overrun steers the CPU/CUDA choice **[MEASURED, then fixed]**

**The defect, as first shipped.** Backend selection (`planner.cpp`) chooses the
dense candidate's backend by comparing modelled seconds, and it ran *before* an
observed floor was applied. So after a GPU run overran its budget the model still
preferred the GPU, the comparison picked it again, and only then did the
time-budget test find that candidate infeasible -- refusing a job the CPU, never
having been tried, was estimated to fit. Reproduced on this machine with the
synthetic fast-device profile from `cuda_backend_selection_test.cpp` (a
12-qubit statevector job, budget 1000 s): initial plan `DirectDense` on CUDA at
8.6987e-05 s estimated; CUDA observed at 2000 s; CPU dense estimated at
0.0794624 s; replan **refused** with "observed runtime of at least 2000 s exceeds
the job's time budget". The refusal message's claim that "no different plan meets
that budget" was untrue.

**It did not violate an acceptance criterion** -- CPU/CUDA selection was
preserved, the strict-budget semantics held, and no result was ever wrong -- but
it was a missed recovery, and it was inside the phase, so it was fixed here.

**The fix.** The rule "an observation can only raise a figure" now lives in one
lambda, `raise_to_observed_floor`, used at **both** places a backend's seconds
are computed: when choosing the dense candidate's backend, and when testing a
candidate against the budget. Each backend's figure is raised to its observed
floor *before* the two are compared, so an observed overrun on one backend steers
the choice to the other; if both backends carry an over-budget floor the chosen
one is infeasible and the job is refused. With no observations both floors are
absent and the comparison is exactly the Phase 10 one, so every first planning,
and every caller that never replans, is unchanged. Nothing about the replan loop,
the runtime or the public API changed.

**Consequence for termination (§5).** The set of things an overrun can exclude is
now `(engine, backend)` pairs, not strategies, because dense can be steered
between two backends.

**Pinned by tests** (all deterministic, on the synthetic profile):

| Test | Case |
|---|---|
| `ACudaOverrunSteersTheDenseRegionOntoTheCpu` | the exact reproduced case: CUDA overran, CPU stays feasible, replan is `DirectDense`/CPU, not `ResourceExceeded` |
| `AnOverrunOnBothBackendsIsStillRefused` | the converse: both backends over-budget, strict refusal |
| `ACpuTheModelSaysIsTooSlowIsStillRefusedAfterACudaOverrun` | steering is not a promise: a CPU the model says is too slow stays refused |
| `ACpuOverrunSteersTheDenseRegionOntoTheGpu` | the mirror image |
| `AFloorBelowTheModelLeavesTheBackendChoiceAlone` | `max()`, never replacement |
| `NoObservationsLeavesBackendSelectionExactlyAsItWas` | first planning unchanged |
| `AGpuOverrunIsAbandonedOnTheDeviceAndRestartedOnTheCpu` | end to end on the real GPU: abandoned at boundary 4 on the device, restarted on the CPU, statevector bitwise equal to a cold CPU run |
| `AnOverrunOnTheGpuThenTheCpuEndsInTheStrictRefusal` | end to end: GPU then CPU each tried exactly once, then refused |

**Mutation check [MEASURED].** Against the pre-fix planner (restored
byte-for-byte afterwards, hash-verified), the four tests that pin the fix fail --
the two steering tests and both run-level tests -- while the four that guard
*preserved* behaviour (both-refused, model-says-too-slow, floor-below-model,
no-observations) correctly pass either way. That split is the point: the fix
changes exactly what it was meant to and nothing else.

---

## 5. Termination **[MENOS]**

Not a chosen cap. Crossing implies `observed_floor(E,B) = elapsed > budget`, so
the engine that was running is **always** infeasible on the replan. Each replan
therefore permanently excludes at least one `(engine, backend)` pair from a fixed
set: the five single-engine `Direct*` strategies with dense counted twice (it can
run on either backend, and §4.2 steers it between them), so **six** pairs. Only a
single-region plan yields a floor and every single-region plan is one of these.
The loop's bound is that six. Floors accumulate in `RuntimeObservations`
and `observe()` keeps the larger of any two, so resetting the per-attempt clock
cannot walk a floor backwards. The loop carries a defensive assertion on that
argument and throws `InternalError` if it is ever violated.

---

### 5.1 "The replan reproduces the plan that overran" cannot happen **[MENOS]**

An earlier draft carried an `InternalError` branch for a replan that returned the
same plan. No test could execute it, and it turns out it is unreachable, so it
was **removed** rather than left as code nothing can run. The proof is written
next to the loop in `src/api/replan_controller.cpp`; in outline:

1. A crossing fires only when `elapsed > budget`, and carries that value.
2. A single-region plan is a single-engine `Direct*` plan: a `Factorized` plan
   has one region per interaction-graph component and is only enumerated with
   at least two (`planner.cpp`, `region_builder.cpp`); a cut plan needs at least
   two fragments (`analysis/cut.cpp`). Multi-region plans are refused before any
   replan (§4).
3. The observation records a floor `>= elapsed > budget` for that plan's
   `(engine, backend)`.
4. `plan_job`, called with the same budget, applies `max(model, floor)` to every
   single-engine candidate and marks it infeasible when that exceeds the budget;
   selection skips infeasible candidates.
5. So a winner with the same strategy, engine and backend would need seconds
   `>= floor > budget` -- it is infeasible and cannot have won.

**The premises are pinned by tests, not left as prose:**

- `RuntimeObservationTest.EveryDirectCandidateIsExcludedByItsOwnObservedFloor`
  checks step 4 for **every** `Direct*` engine -- per candidate, not per winner,
  because `DirectTensorNetwork` is enumerated but never wins on these jobs -- and
  asserts all five engines were covered so it cannot pass by exercising fewer.
- `ReplanTest.EveryReplanInAChainOfCrossingsRunsADifferentEngine` checks the
  conclusion through the real loop: two crossings in a row walk stabilizer → MPS
  → dense, each attempt a different engine, sequence numbered 0 and 1.
- `ReplanTest.ARunWhoseEveryAttemptCrossesEndsInRefusalAfterEachEngineIsTriedOnce`
  shows termination: with every attempt crossing, the job ends in the strict
  refusal after exactly three attempts -- each feasible engine once, none twice.

**Mutation check [MEASURED].** With the floor's effect disabled in the planner
(restored byte-for-byte afterwards, hash-verified), **10 of 22** Phase 11 tests
failed, including both new same-plan tests, so they are not vacuous.

What remains is the loop's attempt bound, which throws `InternalError` if the
argument above were ever broken by a later change. It is the single loud guard
against a livelock -- a bounded loop that fails rather than one that could spin
-- and is **not directly reachable by a test**; what is tested is the invariant
it exists to protect.

## 6. The strict refusal **[MENOS — owner-decided]**

When the budget is crossed and no *different* plan meets it, `menos::run` throws
`common::ResourceExceeded`. The owner chose this over completing the run and
reporting the overrun.

Consequences, stated plainly:

- A job that before Phase 11 returned a correct but late `Result` now throws.
  This affects **only** jobs that set a time budget; nothing else changes.
- A nearly complete run can be discarded. Dense n=22 statevector under a 10.2 s
  budget crosses at ~99% and throws, binning ~10 s of correct work.

This is what "strict" means, and Phase 10 already established the same posture at
plan time.

---

## 7. Architecture

The replan loop lives in `src/api/replan_controller.{h,cpp}`, not in the runtime.
`plan::plan_job` is the only planner in MENOS; a replan is that same function
called again with a non-empty third argument. `Runtime::execute` gains a
`const ExecutionMonitor*` that it passes to engines unchanged; it never creates
one, interprets one, replans, or catches the abandon signal.

`ExecutionMonitor` is declared in `include/menos/simulation_engine.h` because the
engines call it; it is implemented in `src/api` because the api layer is the one
that already owns both the planner and the runtime.

### 7.1 Public API changes

1. `EngineExecutionRequest::monitor` and the `ExecutionMonitor` interface.
   Required: detection must happen well before a single-region run completes
   (§4.1), and there is no runtime-level boundary inside one. Defaulted to
   `nullptr`, so every existing construction site is unchanged.
2. `ReplanEvent` and `Result::replans()`, plus `Result::with_replans()`.
   Required by gate criterion 7. `with_replans` is rvalue-qualified so attaching
   the history does not copy a statevector of up to 67 MB.
3. `RunOptions::time_budget_seconds` exposed in Python — see §9.

`Strategy`, `Engine`, `Backend`, `Plan`, `Plan::explain()`, `CostEstimate`,
`CandidateSummary`, `HardwareProfile` and the `menos::run`/`preview` signatures
are **unchanged**.

---

## 8. `explain()`

`Plan::explain()` is unchanged, byte for byte. `Result::explain()` gains a block
emitted **only** when something was abandoned, so a run that never replans —
which is every run that sets no budget — produces a report identical to a
pre-Phase-11 one.

```
replan 0   threshold=1e+09 s crossed at elapsed=2e+09 s
           abandoned after 6 safe boundaries on engine=stabilizer backend=cpu
           was  strategy=direct_stabilizer predicted=108 s
           reason="observed runtime exceeded the time budget; stabilizer on cpu is excluded by an observed floor of 2e+09 s"
           now  strategy=direct_mps
actual     state_bytes=384 execute_s=0.000059 sample_s=0.000012
```

The full superseded `Plan` is reachable as
`Result::replans()[i].superseded_plan` and has its own `explain()`; only a
one-line summary is printed, to keep the report readable.

`ReplanEvent` carries `boundaries_crossed` with **no accompanying total**,
deliberately: a boundary is a gate, an observable term or a shot depending on the
engine and the phase of its work, and those are not commensurable, so any single
"of N" figure would be a half-truth.

---

## 9. `time_budget_seconds` in Python — reversing `docs/adr/0014` §16.1

Phase 10 recorded that the field would be architecturally consistent to expose
but had **no caller**, and deferred it on prime directive 2. Phase 11 supplies
the caller: the budget is what triggers monitoring and replanning, so keeping it
C++-only would have put this entire phase out of reach of the primary entry
point. That ADR named the revisit trigger ("a real caller") and the exact change
("one line in `src/python/bindings.cpp`, plus a pytest"); both were followed.

The Qiskit `BackendV2` adapter still sets only `shots`, `seed_simulator` and
`want_statevector`, so `RunOptions` remains the low-level escape hatch. A test
asserts the adapter's option surface did not widen.

---

## 10. Checkpointing and state migration — NOT implemented

Phase 11 requires migration only if measurement shows restart is materially
costly. It is not **[MEASURED]**:

- one replan (one `plan_job` call) was measured at **0.052 ms to 17.2 ms mean**
  across four workloads, with a worst single sample of **85 ms** on the workload
  whose mean was 17.2 ms -- different statistics, not one range. Relative to the
  execution being decided about, it is small only when that execution is long:
  **0.0005%** (statevector n=22, 10.6 s) and **1.3% mean / 6.2% worst sample**
  (counts n=20, 1.37 s). It is **not** small for millisecond-scale executions:
  **9%** (expectation n=24, 72 ms) and **~300%** (expectation n=16, 0.5 ms) in the
  same measurement, and 68%–213% for the four Clifford and factorized jobs in the
  first probe, which ran 0.26 ms to 15 ms;
- restarting pays whenever the crossing is detected before 83% of the run
  (§4.1);
- the discarded work is bounded by the budget itself, by construction.

**What this supports, and what it does not.** It supports "no migration" in the
regime where discarded work dominates the cost of a restart -- executions of
about a second or more -- which is where checkpointing would matter. It does not
claim a replan is cheap relative to a millisecond-scale job, where the planning
pass costs about as much as the run; by construction such a job has at most its
budget's worth of work to discard, so there is little for migration to save.
(An earlier revision of this section quoted "0.05 ms–85 ms ... 0.0005%–1.2% of
the executions it decides about". That mixed a mean with a worst-case sample and
claimed more than the four-workload measurement shows; it has been replaced.)

**No migration ADR is required and none is written.**

---

## 11. What was deliberately not done

| | |
|---|---|
| Checkpointing / state migration | §10: restart measured cheap for executions of about a second or more |
| Per-engine observed *rates* | §4: measured wrong by 4×–6× on MPS |
| Observed floors for `Factorized`/`CutAndRecombine` | §4: would require attributing elapsed time per region, which nothing measures; those models are already accurate to 0.70×–1.33× |
| Per-operation boundaries in the tensor-network engine | §3: its `execute()` is a contraction, so a single-term observable is unmonitorable mid-run |
| Monitoring the dense sampler per shot | §3.1: one O(2^n) pass; 16M clock reads at n=24 |
| A CLI `--time-budget` flag | no requirement; the C++ and Python APIs both reach it |
| Heterogeneous per-region CPU/CUDA selection | still deferred (`docs/adr/0014` §16.2); this phase adds no mechanism for it |
| Specialised CUDA kernels | still deferred (`docs/adr/0014` §16.3) |

---

## 12. Limitations of the evidence

- Every measurement is from **one machine** (16 cores, RTX 3050, GCC 15.2). The
  deviation ratios are properties of the cost model and would be expected to
  hold in shape elsewhere, but the absolute figures are this machine's.
- The MPS 42×–1167× figures come from one circuit family (H/T/CX layers). Other
  families were not swept.
- The real-hardware monitoring test accepts three outcomes (replan, refusal, or
  an in-budget completion), because the exact one depends on machine speed. The
  *decision* is pinned deterministically by the injected-clock tests instead.
- Overhead was measured on a laptop with CPU frequency scaling enabled; Google
  Benchmark warned about it. The stabilizer figure (+3.1%) is the one that
  matters and is well above the noise floor of the other two.
- `combine_counts` monitoring is exercised by unit coverage, not by a measured
  multi-region overrun: a multi-region plan is refused on crossing (§4), so the
  path is reachable but yields no replan.
- **A GPU overrun used to be refused even when the CPU would fit** -- found while
  verifying this phase, reproduced, and **fixed within it**; see §4.2. Recorded
  here so the history is not silently overwritten.
- **`verify.sh` does not clang-tidy CUDA test files.** Its CUDA tidy stage covers
  `src/` only, and the main stage excludes `cuda.*_test.cpp`. The new CUDA test
  was tidied by hand against `build/cuda-dev`; the gap is pre-existing
  (`cpu_cuda_differential_test.cpp` has the same exposure) and untouched here.
