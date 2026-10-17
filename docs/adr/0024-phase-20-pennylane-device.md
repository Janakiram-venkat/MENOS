# ADR 0024: Phase 20 — PennyLane device

## Status

**Accepted (design); implemented in `b942fd6`; gate not passed.** The owner started Phase 20 on 2026-10-16 as the next interoperability phase, while Phase 18's gate (`docs/adr/0022`) is still open. The owner answered section 9 the same day, taking every recommended option. Branch `phase-20-pennylane-device`, started from `main` at `887b987` (Phase 18 and Phase 19 merged). On 2026-10-17 `scripts/verify.sh` and `scripts/verify-windows.ps1` both exited 0 on `b942fd6` (section 10). That run left two refusals of section 2.2 unmet (section 10.3). At the owner's request they were fixed on top of `b942fd6`, and the gate evidence is recorded for that change (section 10.6).

Labels follow `docs/adr/0014`–`0023`: **[MEASURED]**, **[DERIVED]**, **[MENOS]** (a proposed design decision) and **[OPEN]** (an owner decision, section 9).

---

## 1. Context

`docs/BUILD_PLAN.md`, Phase 20: a PennyLane device `menos.qubit`, registered by an entry point and built on `qml.devices.Device`. It runs circuits, variational algorithms and parameter-shift gradients through MENOS's planner. This is the second frontend after Qiskit, so it is also the point where the BUILD_PLAN allows shared frontend code to be considered (section 3.9).

### 1.1 MENOS today (`887b987`) — [MEASURED] from the source

| Item | Where | State |
|---|---|---|
| Python core | `src/python/bindings.cpp`, module `menos._core` (nanobind 3.1.0) | `Circuit` (`append`, `append_unitary` up to `kMaxUnitaryQubits` = 5, `append_measure`, `append_channel`, `global_phase`), `run`, `run_batch`, `derive_seeds`, `RunOptions`, `Result` |
| Gate table | `src/ir/gate_table.cpp` | X Y Z H S Sdg T Tdg SX SXdg RX RY RZ P U CX CZ SWAP CP CCX. `RX(θ)` = exp(−iθX/2), the same as PennyLane's. |
| Operand order | `docs/concepts/conventions.md` | Little-endian: bit j of an amplitude index (and of a gate matrix index) is qubit `qubits[j]`. |
| Observables | `include/menos/observable.h` | One Pauli-sum `Observable` per job (`RunOptions.expectation`). There is no variance output. An observable whose terms are all identity raises `InternalError` (`docs/reference/limitations.md`). |
| Statevector | engines | Only dense plans produce one; every other engine is infeasible for `want_statevector`. A noisy circuit has none. |
| Probabilities | — | There is no probability output. Counts are hex keys, bit c = clbit c. |
| Noise | `Circuit.append_channel` | Any CPTP Kraus list on 1–5 qubits, trace preservation checked within 1e-12. `NoiseMethod.Exact` (density matrix, default) or `Auto` (trajectories, counts only). |
| Exactness | `Result.exact`, `Result.expectation_standard_error` | From Python, only trajectory runs are non-exact; `AccuracyPolicy` (truncated MPS) is not bound. |
| `run_batch` | `include/menos/batch.h` | One `RunOptions` for every circuit (one observable, one shot count). Circuit i gets the (i+1)-th SplitMix64 output of the seed. |
| Qiskit frontend | `python/menos/qiskit/` | Exact-pinned optional extras (`qiskit==2.5.2`); no entry points in `pyproject.toml`. |

### 1.2 PennyLane today — [MEASURED] from the 0.45.1 wheel (2026-06-26), not executed

- Latest release 0.45.1, Apache-2.0, **Python ≥ 3.11** (3.11–3.14). No PennyLane release supports both 3.10 and 3.14. 0.42.x is the last for 3.10, and it predates the APIs below.
- `qml.devices.Device`. `execute(circuits, execution_config)` is the only abstract method. Either `preprocess`, or `setup_execution_config` with `preprocess_transforms`, may be overridden. Derivatives are optional; without them PennyLane differentiates by parameter-shift through `execute`.
- Registration is by the `pennylane.plugins` entry point; `qml.device(name, **kwargs)` passes extra kwargs to the constructor.
- **Shots on the device are deprecated** (0.43): the device must read `tape.shots` (a `Shots` object, possibly a shot vector). Users write `qml.set_shots` or `qml.qnode(dev, shots=...)`. The constructor's `shots=` still works with a warning, and PennyLane's own device test suite still passes it.
- Ordering: `state()` and `probs()` are **big-endian** (the first wire is the most significant bit), and so are operator matrices.
- `GlobalPhase(φ)` multiplies the state by **e^{−iφ}**.
- `DepolarizingChannel(p)` uses Nielsen & Chuang's convention: each Pauli with probability p/3, not Qiskit's λ. Every channel exposes `kraus_matrices()`.
- Reference devices: `default.qubit` (analytic results exact; finite shots estimated from samples; `seed` kwarg) and `default.mixed` (channels). `reference.qubit` is the minimal new-API template.

## 2. Scope

### 2.1 Accepted — [MENOS], within the owner's answers

- **Device** `menos.qubit`, class `menos.pennylane.MENOSQubitDevice(qml.devices.Device)`, registered in `pyproject.toml` under `[project.entry-points."pennylane.plugins"]`.
- **Constructor:** `wires=None`, `shots=None` (deprecated in PennyLane; forwarded only so the device test suite runs), `seed=None` (Q7), `time_budget_seconds=None`, `backend=None` (`"cpu"`/`"cuda"`, same refusal semantics as `MENOSBackend`), and `noise_method="exact"` if noise is in scope (Q2).
- **Operations:**
  - every PennyLane operation with an exact gate-table equivalent (section 4);
  - any other operation with a matrix on at most 5 wires, as one exact `UnitaryOp`;
  - `GlobalPhase`, `Identity` and `Barrier`;
  - everything else, through PennyLane's own `decompose` transform (section 3.3).
- **Measurements, analytic (`shots=None`):** `expval` and `var` of observables with a Pauli representation (`op.pauli_rep` is not `None`), `probs`, and `state`.
- **Measurements, finite shots:** `expval`, `var`, `probs`, `sample` and `counts`, estimated from samples as `default.qubit` does. Shot vectors are supported.
- **Parameter broadcasting**, through PennyLane's `broadcast_expand`.
- **Gradients:** every PennyLane method that runs only through `execute`. Parameter-shift is the default under `diff_method="best"`.
- **Noise channels,** if Q2 is answered yes (section 3.6).

### 2.2 Refused with `menos.UnsupportedFeatureError`, naming the construct

- Mid-circuit measurements, `qml.cond`, `qml.measure` and resets (Phase 24).
- Observables without a Pauli representation (`Hermitian`, `Projector`, `SparseHamiltonian`, …).
- `density_matrix`, `mutual_info`, `vn_entropy`, `purity`, `classical_shadow`, `shadow_expval`, and `state` with finite shots.
- `Snapshot`.
- `diff_method="backprop"` and `"adjoint"` (Phase 23).
- Program capture (`eval_jaxpr`) and `qml.qjit`/Catalyst.
- An operation PennyLane cannot decompose into the accepted set.

The exception type is Q9.

## 3. Design — [MENOS]

### 3.1 Package and packaging

`python/menos/pennylane/` has three modules:
- `device.py` holds the device class and its `preprocess`.
- `_convert.py` turns a tape into a `Circuit` and `RunOptions`.
- `_results.py` turns `Result` objects into PennyLane's result shapes.

The package imports `pennylane` itself, so `import menos` never needs it.

The new extra is `pennylane = ["pennylane==<Q1>; python_version >= '3.11'"]`, exact-pinned like `qiskit`. It is also added to the `test` extra. The tests use `pytest.importorskip("pennylane")`, so the cp310 wheel test (cibuildwheel) skips them, and they are listed as skipped there.

### 3.2 Preprocessing

The device overrides `preprocess_transforms` and `setup_execution_config` and does not use PennyLane's capability TOML. The TOML is required only by Catalyst, which is out of scope, and an explicit pipeline keeps every refusal under MENOS's control. In order:
1. `validate_device_wires`.
2. Refuse mid-circuit measurements and conditionals (section 2.2). PennyLane's default setup would insert `defer_measurements`; the override sets `mcm_method` to none and refuses instead.
3. `decompose`, with the stopping condition "in section 4, or has a matrix on ≤ 5 wires, or is `GlobalPhase`/`Identity`/`Barrier`", and `skip_initial_state_prep=False`.
4. `validate_observables`: a Pauli representation is required.
5. `validate_measurements`: the analytic and finite-shot sets of section 2.1.
6. For finite shots only: `split_non_commuting`, `diagonalize_measurements`, then `measurements_from_counts`. Every finite-shot measurement then comes from one MENOS counts run per tape and shot bin, as `reference.qubit` does.
7. `broadcast_expand`.

### 3.3 Operations without an exact table equivalent

An operation with a matrix on ≤ 5 wires becomes `append_unitary(op.matrix(), qubits)`. Its operand list is reversed, because PennyLane matrices are big-endian and MENOS's are little-endian (section 4).

An operation with no matrix, or one on more than 5 wires, is left to PennyLane's `decompose`, the same transform `default.qubit` uses. The resulting operations, and therefore the global phase, are PennyLane's own, which is why `state()` can be compared with `default.qubit` including the global phase. MENOS adds no decomposition of its own.

### 3.4 Analytic execution

Each tape becomes one `Circuit` with no measurements and `shots = 0`.
- `state()`: `want_statevector`. The little-endian vector is reordered to PennyLane's big-endian wire order. A plan that cannot hold a dense vector raises `ResourceExceeded`, as everywhere in MENOS.
- `probs(wires)`: from the same statevector, |ψ|² summed over the other wires. MENOS has no probability output, so analytic `probs` needs a dense plan. Noisy analytic `probs` is refused (Q2).
- `expval(O)`:
  - The identity term of `O.pauli_rep` is added in Python, which is exact. This avoids the all-identity `InternalError`, and an observable that is only identity needs no run.
  - The remaining terms become one `Observable` and one `run` with `RunOptions.expectation`. This path keeps light-cone reduction, Pauli propagation, stabilizer, MPS and tensor-network plans available.
  - The real part is returned. A non-Hermitian sum cannot arise from a Pauli representation of a PennyLane observable.
- `var(O)`: Q6. Proposed: ⟨O²⟩ − ⟨O⟩², where O² is PennyLane's exact `PauliSentence` product. That is two expectation runs, or none for a single Pauli word, whose variance is 1 − ⟨P⟩².
- **Exactness check:** every analytic result must have `Result.exact == True` and `expectation_standard_error is None`. Otherwise the device raises; it never returns a statistical value as analytic (Q3). Under analytic execution the device never sets `NoiseMethod.Auto` or `expectation_precision`, so on `887b987` this check is a guard that should never fire.
- Several measurements in one tape: one run per `expval`/`var`, plus at most one statevector run shared by every `state`/`probs`.

### 3.5 Finite-shot execution

After step 6 of section 3.2, the tape ends in one `counts()` over all wires in a diagonal basis.
- The device measures every wire and runs MENOS with `shots` equal to the bin size, once per shot-vector bin, each with its own seed (Q7, Q8).
- MENOS's hex counts are converted to PennyLane's bitstring keys: the first wire is the leftmost character.
- PennyLane's `measurements_from_counts` post-processing then produces `expval`, `var`, `probs`, `sample` and `counts`.
- `sample()` returns per-shot rows. MENOS returns counts, not a shot sequence, so the rows are the counts expanded in a seeded random order (Q8).

### 3.6 Noise (if Q2 is yes)

- **Channels.** Every PennyLane `Channel` on ≤ 5 wires becomes `append_channel(op.kraus_matrices(), qubits)`, with the operand list reversed as for unitaries. MENOS validates the Kraus list as CPTP within 1e-12 and refuses it otherwise.
  - The Kraus operators are PennyLane's own, so no convention table is needed per channel type, unlike the Aer path (`python/menos/qiskit/noise.py`).
  - `DepolarizingChannel(p)` therefore keeps PennyLane's p/3 convention by construction.
- **Analytic:** `expval` and `var` run on the density-matrix engine with `NoiseMethod.Exact`. `state()` is refused, because a mixed state has none, and `probs` is refused because MENOS has no density-matrix probability output.
- **Finite shots:** counts under `noise_method="exact"` (density matrix) or `"auto"` (trajectories allowed).
- The oracle is `default.mixed`.

### 3.7 Seeds and determinism

Proposed (Q7), mirroring `default.qubit`:
- `seed=int` creates `numpy.random.default_rng(seed)` at construction; `seed=None` seeds that generator from OS entropy.
- Each MENOS run with shots takes the next `rng.integers(0, 2**63)` value, in a fixed order: tape order, then bin order.
- The same device seed and the same sequence of executions therefore give identical results.
- Analytic runs draw no seed.

### 3.8 Batches

Q5. PennyLane hands `execute` a batch of tapes; a parameter-shift gradient is one such batch. `run_batch` takes one `RunOptions` for every circuit, but tapes in a batch differ in observables, shots and measurement kinds. Proposed: one `menos.run` call per required run, each with an explicitly drawn seed (section 3.7). Every circuit still goes through `plan_job` alone, which is the invariant BUILD_PLAN item 3 protects ("no second planner"). The item's wording "through `run_batch`" changes accordingly.

### 3.9 Shared frontend code

This is the second frontend. What the Qiskit and PennyLane adapters both need is:
- the opcode map (different source names),
- operand reversal for a big-endian matrix (Qiskit's `to_matrix()` is little-endian, so the Qiskit adapter does not reverse),
- Pauli-to-`Observable` packing (different source types),
- hex-key decoding.

Each overlap is a few lines and differs in its input type. Proposed (Q10): extract nothing in this phase, and record the overlap here as the evidence a later extraction needs.

### 3.10 Unchanged

There are no C++, planner, engine or `menos._core` changes. `menos.__all__` and the `docs/reference/python-api.md` contract block are unchanged; `menos.pennylane` is a subpackage. `MENOSBackend.max_circuits` stays 1 unless Q4 says otherwise.

## 4. Conventions — each row has a test

| Convention | Rule |
|---|---|
| Wire to qubit | Device wires given: wire `wires[k]` is MENOS qubit `k`. Device wires `None`: `tape.wires` order, the order PennyLane's `map_to_standard_wires` uses. |
| Gate matrices | PennyLane's matrix for wires `(w0, …, wk−1)` is big-endian. Through `append_unitary` the operands are `(q(wk−1), …, q(w0))`. Tested entrywise within 1e-12 against `op.matrix()`. |
| Gate table | `PauliX`→X, `PauliY`→Y, `PauliZ`→Z, `Hadamard`→H, `S`→S, `T`→T, `SX`→SX, `Adjoint(S)`→Sdg, `Adjoint(T)`→Tdg, `Adjoint(SX)`→SXdg, `RX`/`RY`/`RZ`→RX/RY/RZ, `PhaseShift`→P, `U3`→U, `CNOT`→CX (control first), `CZ`→CZ, `SWAP`→SWAP, `ControlledPhaseShift`→CP, `Toffoli`→CCX. Each pair is equal entrywise within 1e-12, global phase included. |
| `GlobalPhase(φ)` | `Circuit.global_phase −= φ`, because PennyLane multiplies by e^{−iφ}. |
| `state()` | Full state over the device wires (or the tape wires), big-endian, global phase included. |
| `probs(wires)` | Ordered by the wires as the user lists them, first wire most significant. |
| `counts`/`sample` | Keys and rows have the first wire leftmost; outcome values are 0/1. |
| `Identity`, `Barrier` | No operation. |
| Channels | `kraus_matrices()` with the same operand reversal as gates. |
| Seeds | Section 3.7. |

## 5. Testing — [MENOS]

1. **Conventions:** one test per row of section 4.
2. **Gate table:** every mapped name, and the `UnitaryOp` path for 1–5 wires, against `op.matrix()` within 1e-12, global phase included.
3. **Analytic differential against `default.qubit`:**
   - Fixtures: the `tools/gen_fixtures.py` circuits rebuilt in PennyLane, plus hand-written ones covering decomposed templates, `GlobalPhase`, broadcasting and non-integer wire labels.
   - Outputs compared: `state` (global phase included), `probs`, `expval` and `var`.
   - Proposed threshold: 1e-12 (Q11).
4. **Finite shots:**
   - `counts`/`sample` distributions against `default.qubit`'s exact probabilities, by chi-square at a family-wise 0.001 with a Bonferroni correction (Phases 13–14).
   - Shot vectors keep their bin structure.
   - A fixed seed gives identical results twice; different seeds differ.
5. **Gradients:** the parameter-shift gradient of a VQE fixture (a hand-written Pauli-sum Hamiltonian, so no `qchem` dependency) against `default.qubit` within 1e-12, and against `default.qubit` with backprop within 1e-10.
6. **Noise (Q2):** expval and var against `default.mixed` within 1e-12; counts by chi-square; the refusals of section 3.6.
7. **Refusals:** each item of section 2.2, asserting the exception type and the construct's name.
8. **PennyLane device test suite:**
   - `pennylane.devices.tests`, run with `shots=None` and with a finite shot count.
   - Every skip or expected failure is listed.
   - Its `skip_if` checks read the old device-capabilities dictionary, so some tests are skipped for any new-API device; the list records which.
9. **Entry point:** `qml.device("menos.qubit", wires=2)` from the installed wheel (wheel smoke test).
10. **Unchanged behaviour:** the existing Python and C++ suites and the CLI golden files pass unmodified.

## 6. Order of work

1. This ADR, the `docs/BUILD_PLAN.md` Phase 20 status, and the owner's answers.
2. Measure: pin the PennyLane version and add it to the `test` extra in a scratch environment. Record the device test suite's behaviour on `reference.qubit` (its skips under `skip_if`) as a baseline.
3. `_convert.py`: operations, wires, global phase, the conventions tests and the gate-table tests.
4. `device.py`: preprocessing, analytic execution and the refusals.
5. Finite shots, shot vectors and seeds.
6. Noise (Q2).
7. The device test suite, the gradient fixture, and the differential and chi-square tests.
8. Packaging: the entry point, the extras, the wheel smoke test and cibuildwheel.
9. Documentation: README, `docs/reference/compatibility.md`, `limitations.md`, the changelog, a frontend page with a runnable snippet, and an example covered by `test_examples.py`.
10. Gate evidence (section 10): `scripts/verify.sh` in the WSL clone and `scripts/verify-windows.ps1`.

## 7. Out of scope

- Adjoint differentiation and device derivatives (Phase 23).
- Mid-circuit measurement, `qml.cond` and reset (Phase 24).
- Catalyst/`qjit`, program capture and the capability TOML.
- Observables without a Pauli representation.
- Density-matrix outputs and analytic noisy `probs`.
- Readout error, since PennyLane has no per-measurement readout channel; `default.mixed`'s `readout_prob` is not mapped.
- Python 3.10 support for the PennyLane extra.
- Any C++, planner, engine or Qiskit-adapter change; shared frontend code (Q10); `MENOSBackend.max_circuits` (Q4).

## 8. Risks

| Risk | How it is caught |
|---|---|
| Big- versus little-endian reversal mistaken for one path | Conventions tests for `state`, `probs`, `counts` and `append_unitary` on asymmetric fixtures, with non-contiguous and non-integer wire labels |
| A PennyLane decomposition differs from `default.qubit`'s path by a global phase, visible in `state()` | Both devices run the same `decompose` output; the differential test compares including the global phase |
| `default.qubit` agrees only to ~1e-13 on deep fixtures | The threshold is an owner decision (Q11); a miss goes to the owner, never a tolerance change |
| PennyLane removes constructor `shots` (no version announced) | The device reads only `tape.shots`; the constructor argument is kept only for the test suite and is pinned with the version |
| The device test suite has failures outside the supported set | Listed in section 10 with the reason; never hidden by `--skip-ops` alone |
| Many `expval`s in one tape cost one run each | Measured on the gradient fixture and recorded; batching several observables into one job is a C++ API change for a later ADR |
| Per-run `seed` draws differ from `run_batch`'s derivation | `derive_seeds` is not used; section 3.7 is tested on its own |

## 9. Owner decisions

- **Q1 — PennyLane version and Python range.**
  - (a, recommended) `pennylane==0.45.1`, exact-pinned like `qiskit==2.5.2`. The extra is available on CPython 3.11–3.14 only, through an environment marker. On 3.10, `import menos.pennylane` raises `ImportError` naming the requirement, and the cp310 wheel test skips the PennyLane tests.
  - (b) A range `>=0.43,<0.46`, tested at both ends.
- **Q2 — Noise channels.**
  - (a, recommended) In scope, as section 3.6: any PennyLane channel on ≤ 5 wires through its own Kraus matrices; analytic `expval`/`var` and finite-shot counts; `noise_method="exact"|"auto"`; `default.mixed` as the oracle.
  - (b) Refused, deferred.
- **Q3 — `shots=None` with a non-exact plan.**
  - (a, recommended) Always refused. No opt-in in this phase, because the device never requests a non-exact method under analytic execution, and the check of section 3.4 guards it.
  - (b) An opt-in device option that returns the statistical value and reports its standard error.
- **Q4 — `MENOSBackend.max_circuits`.**
  - (a, recommended) Unchanged (1). It is a Qiskit-adapter concern and is not needed by this device.
  - (b) Lifted in this phase.
- **Q5 — `run` versus `run_batch`.**
  - (a, recommended) One `menos.run` per required run, as in section 3.8, with the BUILD_PLAN item 3 wording changed from "through `run_batch`" to "each circuit planned alone by `plan_job`".
  - (b) Group runs with identical `RunOptions` into `run_batch` calls, which ties results to grouping and to `run_batch`'s seed derivation.
- **Q6 — `var`.**
  - (a, recommended) Analytic ⟨O²⟩ − ⟨O⟩² from two exact runs, with O² computed exactly by `PauliSentence` multiplication; 1 − ⟨P⟩² for a single Pauli word.
  - (b) Analytic `var` refused.
- **Q7 — Seeds.**
  - (a, recommended) `seed: int | None`, with a NumPy generator per device as in section 3.7. `None` is OS entropy, as `default.qubit`'s `seed=None`; `default.qubit`'s `"global"` is not supported.
  - (b) The `MENOSSampler` rule: a fresh `secrets.randbits(63)` per `execute`, with derived seeds per run.
- **Q8 — Finite-shot semantics and `sample()` order.**
  - (a, recommended) PennyLane's semantics: every finite-shot measurement is estimated from one counts run per tape and shot bin. `sample()` rows are the counts expanded in a random order drawn from the run's seed.
  - (b) Rows in ascending outcome order, as `MENOSSampler`'s `BitArray`. This is not exchangeable, so statistics computed on part of the rows would be biased.
- **Q9 — Exception type for refusals.**
  - (a, recommended) A device exception that is both `menos.UnsupportedFeatureError` (the BUILD_PLAN invariant) and PennyLane's `DeviceError`, so PennyLane's tooling and `pl-device-test --skip-ops` recognise it. This is subject to a measurement in step 2 that nanobind's exception type can take part in multiple inheritance; if it cannot, the device falls back to (b) and says so in section 10.
  - (b) `menos.UnsupportedFeatureError` only.
- **Q10 — Shared frontend code.**
  - (a, recommended) None extracted in this phase; the overlap is recorded in section 3.9.
  - (b) Extract the opcode map and Pauli packing into `menos._frontend`.
- **Q11 — The gate.** As in `docs/BUILD_PLAN.md`, Phase 20, refined as:
  1. Conventions, one test per row of section 4.
  2. Gate table and `UnitaryOp` path within 1e-12, global phase included.
  3. Analytic differential against `default.qubit` within 1e-12: `state` (global phase included), `probs`, `expval`, `var`.
  4. Finite-shot chi-square at a family-wise 0.001 (Bonferroni); shot vectors; seeded determinism.
  5. The parameter-shift VQE gradient within 1e-12 of `default.qubit`'s.
  6. Noise, if Q2 is yes: against `default.mixed` within 1e-12, and chi-square for counts.
  7. Every refusal of section 2.2 by name.
  8. PennyLane's device test suite passes for the supported features, `shots=None` and finite, with every skip listed.
  9. Entry point from the installed wheel; wheels build with the extra marker.
  10. The Qiskit adapter's tests and every existing suite pass unmodified; `scripts/verify.sh` exits 0 in the WSL clone and `scripts/verify-windows.ps1` exits 0, both on the final commit; documentation updated; every new symbol has a production caller and tests.

### Owner answers (2026-10-16)

The owner took the recommended option, (a), for every question:

- **Q1:** `pennylane==0.45.1`, exact-pinned, CPython 3.11–3.14 only.
- **Q2:** noise channels are in scope, as section 3.6.
- **Q3:** a non-exact result under `shots=None` is always refused; there is no opt-in.
- **Q4:** `MENOSBackend.max_circuits` stays 1.
- **Q5:** one `menos.run` per required run; BUILD_PLAN item 3 is reworded.
- **Q6:** analytic `var` is ⟨O²⟩ − ⟨O⟩² from exact runs.
- **Q7:** one NumPy generator per device, as section 3.7.
- **Q8:** PennyLane's finite-shot semantics; `sample()` rows in a seeded random order.
- **Q9:** a device exception that is both `menos.UnsupportedFeatureError` and PennyLane's `DeviceError`, subject to the step-2 measurement, with (b) as the recorded fallback.
- **Q10:** no shared frontend code is extracted.
- **Q11:** the gate as written in Q11.

## 10. Gate evidence (`b942fd6`, 2026-10-17)

### 10.1 Verification runs

- **Linux.** `scripts/verify.sh` in the WSL clone `~/MENOS` on `b942fd6`, `UV_PYTHON_PREFERENCE=only-managed`, exit 0. Format and clang-tidy (all targets) clean; `dev` and `release` 679/679; pytest 548 passed, 2 deselected (`test_wheel_smoke.py`), 3 warnings (one PennyLane `UserWarning` raised inside the refusal test for `probs` of a `Hermitian`, two Qiskit deprecation warnings in `test_qasm3.py`); wheel smoke 2 passed in a clean CPython 3.14 venv; `cuda-dev`/`cuda-release` and `cuda12-dev`/`cuda12-release` 734/734. The documentation site was not built (no Node.js in WSL), as in Phase 19; its Python snippets ran in the pytest stage.
- **Windows.** `scripts\verify-windows.ps1` (without `-Wheels`) on `b942fd6`, exit 0 (`verify-windows: all stages passed`). `windows-dev` and `windows-release` 680/680; pytest 548 passed, 2 deselected; wheel smoke 2 passed.
- A first Windows run, made while `verify.sh` was running in WSL at the same time, failed three `windows-dev` tests (`TrajectoryRunTest.CountsPassAChiSquareTestAgainstTheDensityMatrix`, `ReadoutMitigationRunTest.ReadoutOnlyMitigationAgreesWithTheReferenceWithinItsErrors`, `ReadoutMitigationRunTest.AMitigatedResultIsNeverExactAndExplainsItself`). Each was AddressSanitizer out of memory on a 32 MiB allocation in `measure_memory_bandwidth` (`src/plan/hardware_profile.cpp`), with 7 GB of 31.7 GB free. The three passed when re-run alone, and the full run above was made with nothing else running. Phase 20 changes no C++.
- The pytest count grew from 313 at Phase 19 (`bb720d8`) to 548: 234 new tests in `python/tests/test_pennylane_*.py` and the `pennylane_device` example in `test_examples.py`. Every one passed on both platforms (none skipped).

### 10.2 Criteria

| # | Criterion | Evidence |
|---|---|---|
| 1 | Conventions, one test per row of section 4 | `test_pennylane_conventions.py`: wire to qubit with and without device wires (non-integer, non-contiguous labels), the `UnitaryOp` operand order, the gate table, the `GlobalPhase` sign, big-endian `state()`, `probs` in the listed wire order, `counts`/`sample` first wire leftmost, `Identity`/`Barrier` as no operation, channel operand order (an asymmetric two-wire channel against `default.qubit`/`default.mixed`), and seeds. All passed. |
| 2 | Gate table and `UnitaryOp` path within 1e-12, global phase included | `test_gate_table_matches_pennylane_matrix` for all 20 names of `GATE_MAP` (`test_gate_map_covers_every_name` checks coverage); `test_unitary_path_matches_pennylane_matrix` for random unitaries on 1–5 wires; `test_unitary_path_for_unmapped_named_gate` (`CRX`). Each compares the matrix the device applies, column by column through `state()`, with `op.matrix()` at `atol=1e-12`, `rtol=0`. All passed. |
| 3 | Analytic `state`/`probs`/`expval`/`var` within 1e-12 of `default.qubit` | `test_pennylane_analytic.py`: every circuit of `tests/ir/fixtures/random_circuits.json` rebuilt in PennyLane (the rebuild is first checked against the fixture's Qiskit oracle), plus decomposed templates (`QFT`, `StronglyEntanglingLayers`, `BasicEntanglerLayers`, `BasisState`, `StatePrep`, a 6-wire `MultiControlledX`, `ctrl(RY)`, controlled `GlobalPhase`, `Rot`/`CRot`, `IsingXX`/`SISWAP`), `probs(op=…)`, string wire labels and parameter broadcasting, all at 1e-12 with the global phase included. Also the identity-term path, the single-Pauli variance and the Q3 exactness guard (`test_non_exact_analytic_result_is_refused`). All passed. |
| 4 | Finite-shot chi-square at a family-wise 0.001; shot vectors; seeded determinism | `test_counts_chi_square_against_default_qubit`: 8 fixtures of 2–4 qubits, 20,000 shots each, each at 0.001/8 (Bonferroni). `test_shot_vector_structure_matches_default_qubit`: a shot vector `[100, (200, 2), 50]` over seven measurement kinds has `default.qubit`'s result structure. `test_same_seed_gives_identical_results`, `test_seeds_reproduce_and_differ`, `test_samples_are_not_sorted` (Q8) and `test_finite_shot_expval_is_estimated_from_samples`. All passed. |
| 5 | Parameter-shift VQE gradient within 1e-12 of `default.qubit`'s | `test_pennylane_gradient.py`: a 4-qubit, 9-term hand-written Hamiltonian, a two-layer ansatz with 24 parameters. Energy and parameter-shift gradient within 1e-12 of `default.qubit`'s; within 1e-10 of `default.qubit`'s backprop gradient; `diff_method="best"` runs parameter-shift through `execute`. All passed. |
| 6 | Noise against `default.mixed` within 1e-12; chi-square for counts | `test_pennylane_noise.py`: nine circuits (depolarizing, amplitude damping, generalized amplitude damping, phase damping, bit and phase flip, `PauliError`, `ThermalRelaxationError`, `ResetError`, a random two-qubit `QubitChannel`): `expval` and `var` within 1e-12; counts by chi-square under `noise_method="exact"` and `"auto"` (18 tests at 0.001/18). Noisy analytic `state`/`probs` refused; analytic runs stay on `NoiseMethod.Exact` under `"auto"`; a Kraus list outside MENOS's 1e-12 CPTP check is refused, not renormalised. All passed. |
| 7 | Every refusal of section 2.2 by name | `test_pennylane_refusals.py`: mid-circuit measurement, `qml.cond`, reset, `expval` of a measurement value, `Hermitian`, `Projector`, `SparseHamiltonian`, a `Hadamard` observable, a non-Pauli factor of a product, `density_matrix`, `mutual_info`, `vn_entropy`, `purity`, `classical_shadow`, `shadow_expval`, `state` with shots, `probs` of a non-Pauli observable, `Snapshot`, `sample`/`counts` with `shots=None`, and an undecomposable operation. Each raises `MENOSDeviceUnsupportedError`, is both `menos.UnsupportedFeatureError` and `DeviceError`, and names the construct and `menos.qubit`. On `b942fd6`, two items of section 2.2 were not met (section 10.3); both are fixed in section 10.6. |
| 8 | PennyLane's device test suite passes for the supported features, with every skip listed | `test_pennylane_device_suite.py` runs `pennylane.devices.tests` (426 tests) with `--skip-ops` and fails on any failure that is not a refusal by design; passed on both platforms. Outcomes measured separately in the WSL venv: **`shots=None`:** 345 passed, 37 skipped, 9 xfailed, 35 refused by design. **`shots=10000`:** 338 passed, 52 skipped, 9 xfailed, 27 refused by design. The skips and refusals are listed in section 10.4. |
| 9 | Entry point from the installed wheel; wheels build with the extra marker | `test_wheel_pennylane_entry_point` (`qml.device("menos.qubit", …)` and a Bell-pair counts run) passed from a clean venv with the built wheel on Linux and Windows. The wheel's metadata carries `Provides-Extra: pennylane`, `Requires-Dist: pennylane==0.45.1; python_version >= "3.11" and extra == "pennylane"` and the `[pennylane.plugins]` entry `menos.qubit = menos.pennylane.device:MENOSQubitDevice`. The cibuildwheel matrix (`verify-windows.ps1 -Wheels`, CPython 3.10–3.14) was not run. |
| 10 | Existing suites unchanged; both verify scripts exit 0; documentation; production callers and tests | Both scripts exit 0 on `b942fd6` (section 10.1). The C++ suites (including the CLI golden tests) are the same 679 (Linux) and 680 (Windows) tests as at Phase 19, all passing; no C++, planner, engine or `menos._core` file changed. Two existing Python test files changed, both additively: `test_examples.py` skips `pennylane_*` examples when PennyLane is not installed, and `test_wheel_smoke.py` gained the entry-point test. Documentation: README, `docs/build/pennylane.md`, `docs/examples/pennylane.md`, `docs/get-started/installation.md`, `compatibility.md`, `limitations.md`, `changelog.md`, `docs/BUILD_PLAN.md`, this ADR; example `examples/pennylane_device.py`, run by `test_examples.py`. New symbols: `MENOSQubitDevice` is reached through the entry point, and `MENOSDeviceUnsupportedError` through every refusal; the `_convert.py` functions are called only by `device.py`; each is tested above. |

### 10.3 Criterion 7 on `b942fd6` (fixed in section 10.6)

1. **Program capture and `qml.qjit`/Catalyst (section 2.2) had no refusal.** The device did not implement `eval_jaxpr`, so capture reached the base class's bare `NotImplementedError`. Catalyst refused with its own `CompileError` ("Config file (…/menos_qubit.toml) does not exist"), measured with Catalyst 0.15.0.
2. **`diff_method="backprop"` and `"adjoint"` were refused by PennyLane, not by the device.** PennyLane raised its own `QuantumFunctionError` naming `menos.qubit`, which is not `menos.UnsupportedFeatureError` as section 2.2 states.

The owner chose a code change over accepting either item as recorded.

### 10.4 Device test suite: skips and refusals (`b942fd6`, WSL venv)

Refused by design (`MENOSDeviceUnsupportedError`, naming the construct): observables `Hermitian`, `Projector`, `SparseHamiltonian` and `Hadamard` (alone or as a factor of a product), `Snapshot`, and the suite's custom `MyMeasurement` class. 35 tests with `shots=None`, 27 with `shots=10000`.

Skipped or xfailed, with the suite's own reason (count with `shots=None` / `shots=10000`):

| Reason | None | 10000 |
|---|---|---|
| "Device is in analytic mode, cannot test sampling." | 10 | 0 |
| "Device is in non-analytical mode." | 0 | 17 |
| "test is old interface specific." (8) and three other old-device-interface messages (1 each) | 11 | 11 |
| xfail "most devices do not support CV" | 8 | 8 |
| "device does not support diff_method" (`TestGradients`, backprop variants: `test_basic_grad`, `test_backprop_state`, `test_parameter_shift`, `test_probs`, `test_multi_meas`, `test_hessian`) | 6 | 0 |
| "collection skipped" (`test_gradients_jax`, `test_gradients_torch`: JAX and Torch not installed) | 2 | 2 |
| "test only works with backprop" | 2 | 2 |
| "QutritBasisState template only works on qutrit devices" | 2 | 2 |
| "operation not supported on the device" (`test_supported_gates_can_be_implemented[QubitDensityMatrix]`: mixed-state preparation, out of scope as a density-matrix construct) | 1 | 1 |
| "test only works with parameter-shift" | 1 | 1 |
| xfail "Device does not support custom measurement transforms." | 1 | 1 |
| "Shots must be specified in the device to compute a sampled measurement." | 1 | 0 |
| "If shots=None no warning is raised." / "If shots!=None no error is raised." | 1 | 1 |
| "test should only be run in analytic mode" | 0 | 6 |
| "test only works with analytic-mode simulations" | 0 | 4 |
| "SparseHamiltonian only supported in analytic mode" | 0 | 2 |
| "test only works with analytic-mode pure statevector simulators" | 0 | 2 |
| "Some plugins don't update state information when shots is not None." | 0 | 1 |

### 10.5 As built, differing from section 3

- There is no `_results.py`: result conversion lives in `_convert.py` (`statevector_to_big_endian`, `counts_to_samples`) and `device.py`.
- Finite-shot measurements are computed by each measurement's own `process_samples` on the expanded rows, after `split_non_commuting` and `diagonalize_measurements`, instead of through `measurements_from_counts` (section 3.2 step 6). The semantics are PennyLane's (Q8), and criterion 4's tests cover them.
- `setup_execution_config` is not overridden. Mid-circuit measurements reach `_refuse_unsupported` and are refused by name (criterion 7 tests), without PennyLane's `defer_measurements` being applied first.
- Q9: the step-2 measurement is the refusal tests themselves. `MENOSDeviceUnsupportedError(UnsupportedFeatureError, DeviceError)` works with nanobind's exception type, and every refusal test asserts both `isinstance` checks, so the fallback (b) was not needed.
- Not recorded: the step-2 `reference.qubit` baseline of the device test suite, and the section 8 cost measurement of one run per `expval` on the gradient fixture. Neither is a gate criterion.
