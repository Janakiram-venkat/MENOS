# ADR 0004: Qiskit BackendV2 and the Python boundary

## Status

Accepted.

## Context

`docs/BUILD_PLAN.md` Phase 2 requires a Python package exposing a
standards-conforming `BackendV2` that executes circuits through the
existing C++ KIR/runtime (Phase 1), with a "hard anti-hallucination rule":
every Qiskit API used must be verified against the installed package, not
remembered. `docs/BUILD_PLAN.md` §2.0 names this file
`docs/adr/0002-qiskit-backendv2-and-python-boundary.md`; `0002` is already
`nlohmann/json` (Phase 1), so this is `0004` — the next free number.

## Investigation (docs/BUILD_PLAN.md §2.0)

Everything below was read directly from **installed Qiskit 2.5.2 source**
in a `uv`-managed venv (`.venv/`, Python 3.14.4) this session, or confirmed
by running code against the installed package. File paths are relative to
`.venv/lib/python3.14/site-packages/qiskit/`.

### Versions

| Package | Version | Note |
|---|---|---|
| Python | 3.14.4 | Only version tested; `pyproject.toml` claims only this |
| Qiskit | 2.5.2 | Same pin as `tools/requirements.txt` (Phase 1 fixtures) |
| nanobind | 3.1.0 | Latest on PyPI at investigation time |
| scikit-build-core | 1.0.3 | Latest on PyPI at investigation time |
| numpy | 2.5.3 | Runtime dependency (statevector arrays) |
| pytest | 9.1.1 | Test-only |

`qiskit-aer` (0.17.2) was checked for compatibility (resolves cleanly) but
is **not** a dependency — nothing in the required test list needs it, and
"minimal dependencies" favors leaving it out.

### `qiskit.providers.basic_provider.BasicSimulator` / `BasicProviderJob`

Qiskit ships a real, maintained, first-party `BackendV2` simulator
(`providers/basic_provider/basic_simulator.py`,
`basic_provider_job.py`). This is the authoritative pattern reference for
nearly every construction question below — an actually-installed
implementation, not a remembered one.

### Compatibility table

| API | Installed signature (file) | MENOS use |
|---|---|---|
| `BackendV2.__init__` | `(provider=None, name=None, description=None, online_date=None, backend_version=None, **fields)` (`providers/backend.py:82`) | `MENOSBackend.__init__` |
| `BackendV2.target` (abstract) | property `-> Target` | Built once per instance from `_gate_table.GATE_TABLE` |
| `BackendV2.max_circuits` (abstract) | property | `1` — multi-circuit `run_input` rejected explicitly |
| `BackendV2._default_options` (abstract) | classmethod `-> Options` | `Options(shots=1024, seed_simulator=None, want_statevector=False)` |
| `BackendV2.run` (abstract) | `(self, run_input, **options)` | Validates, converts, calls `menos._core.run`, adapts, returns `MENOSJob` |
| `Target(description=, num_qubits=)`, `.add_instruction(gate, properties=None, name=...)` | `transpiler/target.py:243,363` | `num_qubits=None` (matches `BasicSimulator`'s own "no fixed device size" convention); `properties=None` becomes global `{None: None}` |
| `Options(**kwargs)` | `providers/options.py` | No `set_validator` calls — `BasicSimulator` itself doesn't validate `shots`, and MENOS's own semantics (`shots=0` is valid) would make a `shots >= 1` validator wrong |
| `JobV1.__init__(backend, job_id, **kwargs)`, `_async` | `providers/job.py:56` | `MENOSJob(backend, job_id, result)`, `_async = False` (matches `BasicProviderJob` exactly) |
| `JobV1.cancel()` default | raises `NotImplementedError` | Not overridden — cancelling an already-completed synchronous job is meaningless |
| `Result.from_dict(dict)` | `result/result.py:110` | Builds the returned `Result` from a dict shaped exactly like `BasicSimulator`'s own |
| `Result.data()["counts"]` hex-string keys | `result/result.py:148` (docstring), `result/postprocess.py` | MENOS `uint64` counts → `hex(key)` strings; Qiskit's own `Counts`/postprocess machinery does binary/register display formatting — the adapter never formats a binary string itself. **Verified empirically**: for a 3-register circuit, the display groups registers space-separated in *reverse declaration order*, each register's own highest bit first (e.g. `"10 1"`, not `"1 10"`) — confirmed by constructing a `Result.from_dict` by hand and reading `get_counts()`, not assumed |
| `Result.data()["statevector"]` | `result/postprocess.py:174` | Accepts a NumPy `complex128` `ndarray` directly (read from the function body) |
| `op.to_matrix()` | `Gate`/`Instruction` | Custom-instruction → KIR `Unitary`. **Both** `AttributeError` (plain `Instruction`, e.g. `reset`) and `CircuitError` (`Gate` with no matrix, and `StatePreparation`) must be caught — confirmed by triggering both cases directly, not assumed |
| `op.is_parameterized()` | `Instruction` | Reject unbound `Parameter`s with `InvalidCircuitError` |
| `circuit.find_bit(bit).index` | `QuantumCircuit` | Global qubit/clbit indices for multi-register circuits |
| `circuit.global_phase` | `QuantumCircuit` | Plain float attribute, never an instruction in `circuit.data` in normal use |
| `if_else`/control-flow ops | `op.name == "if_else"` etc., `.condition` attribute | Rejected by falling through the supported-name table; no special case needed |
| `qiskit.exceptions.QiskitError(*message)` | `exceptions.py` | MENOS exceptions derive from `MenosError`, **not** `QiskitError` — they aren't Qiskit errors, and nothing in the spec asks for that relationship |

All 19 MENOS named opcodes were cross-checked mechanically against
`qiskit.circuit.library.standard_gates.get_standard_gate_name_mapping()`:
same name, arity and parameter count for every one.

### GUESS BLOCKERS

None. Every fact above was read from installed source or confirmed by
execution this session.

## Decisions

- **`menos._core`** (nanobind, `src/python/bindings.cpp`) exposes exactly:
  `Circuit`, `Opcode` (19 named gates only — `Unitary`/`Measure` are
  reached via `Circuit.append_unitary`/`append_measure`), `RunOptions`,
  `Result`, `run()`, `kMaxUnitaryQubits`, and the four exception types
  bound to `menos::common::{InvalidCircuit,UnsupportedFeature,
  ResourceExceeded,InternalError}`. Nothing from `src/plan` or
  `src/runtime` is bound — the Python adapter doesn't consume `Plan`,
  `Region`, or `Strategy` this phase.

- **Exception translator registration order is load-bearing.** Verified
  against nanobind 3.1.0's actual implementation
  (`nanobind/src/nb_internals.h`: "an append-to-front-only singly linked
  list traversed while raising exceptions", `nanobind/src/error.cpp`'s
  `register_exception_translator`): translators are tried
  **most-recently-registered-first**. Because `catch (Base&)` also matches
  derived exceptions, registering `MenosError`'s translator *before* the
  four subclass translators is required — the opposite order would
  collapse every specific exception into the generic base type. This was
  found by reading nanobind's source, not assumed from pybind11 (which
  nanobind resembles but isn't identical to).

- **Statevector is a copy, not a view.** `Result.statevector` allocates a
  fresh NumPy array and copies the C++ `std::vector` into it (`bindings.cpp`'s
  `statevector_to_numpy`). Simpler lifetime story than a zero-copy view
  tied to a temporary `Result`; the spec explicitly prefers a safe copy
  over an unsafe optimization.

- **`shots` default = 1024, matching `BasicSimulator`'s own default** (not
  invented). `seed_simulator` default = `None`; when unset,
  `MENOSBackend.run()` generates a fresh seed via `secrets.randbits(63)`
  (not `BasicSimulator`'s legacy `np.random` global-state pattern) and
  passes a concrete value into `RunOptions.seed` — the C++ layer never
  receives an implicit seed, so MENOS's determinism invariant (same seed →
  same result) is unaffected.

- **Statevector output is an explicit backend option, `want_statevector`**
  (default `False`), independent of `shots`. `shots=0` with
  `want_statevector=False` is rejected by the C++ layer's own
  `validate_output_request` (surfaces as `InvalidCircuitError`) rather than
  silently corrected at the Python layer.

- **Counts stay `uint64_t`-keyed this phase**, matching Phase 1's existing
  64-classical-bit ceiling. Phase 3 introduces the arbitrary-width
  `BitString`; that migration is out of scope here and is not
  pre-implemented.

- **Job lifecycle is fully synchronous**, matching `BasicProviderJob`
  exactly (`_async = False`, `submit()` a no-op, `result()` returns the
  already-computed result, `status()` always `DONE`). No background thread
  is introduced, so no ThreadSanitizer preset is required this phase.

- **`qiskit-aer` is not a dependency.** Confirmed compatible if a future
  phase's tests need it as a pinned dev-only oracle, but nothing in Phase
  2's required test list does.

## Conversion performance (docs/BUILD_PLAN.md §2.7)

Measured with `python/tests/test_conversion_performance.py` on this
machine, a 10-qubit/100,000-operation representative circuit (alternating
`h`/`cx`) and a 5-qubit/200-operation smaller one:

| Circuit | `conversion_s` | `total_submission_s` | `cpp_execute_s` | ratio |
|---|---|---|---|---|
| 5q/200op | 0.00035 | 0.00049 | 0.00005 | 70% |
| 10q/100,000op (representative) | 0.122–0.132 | 0.77–0.82 | 0.64–0.67 | **15–16%** |

The representative-circuit ratio exceeds the spec's 10% threshold, so
per §2.7 some optimization is justified. Profiling (`cProfile` on
`convert_circuit`) showed `QuantumCircuit.find_bit()` — called once per
operand, ~150,000 times for a circuit with only 10 distinct qubits — as a
measurable, cheaply-fixable cost. **Applied**: build a `{Qubit: index}` /
`{Clbit: index}` map once per circuit (`qubit_index`/`clbit_index` in
`convert.py`) instead of calling `find_bit()` per operand; this is exactly
equivalent (`find_bit(bit).index` is that same position within
`qc.qubits`/`qc.clbits`, verified by the unchanged differential/register
test results after the change) and requires no new `menos._core` API.
This brought the ratio down from ~16.6% to ~15.2% (three-run average) —
real but modest.

**Applied in Phase 3** (owner asked for this as a preliminary step before
Phase 3 proper, closing the item above): `Circuit::append_batch` — a
CSR-style batched ingestion API for named-gate operations (`include/menos/circuit.h`),
bound in `bindings.cpp`, used by `convert.py` to accumulate a full
circuit's named gates into flat Python lists and flush them in one call
instead of one `Circuit.append()` call per gate. Differentially tested
(`tests/ir/append_batch_test.cpp`, `tests/api/run_test.cpp`) against the
equivalent sequence of individual `append()` calls — byte-identical
`Circuit` contents and execution results.

Re-profiling after this change (`cProfile` on `convert_circuit`, same
representative circuit) confirms it did exactly what it was designed to
do: the ~100,000 Python→C++ boundary crossings collapsed to 11 calls
(one `append_batch` flush per contiguous named-gate run, plus individual
`append_measure` calls), and that flushing now accounts for **~2%** of
`convert_circuit`'s total time (down from being architecturally the
dominant per-operation cost). A first draft that recomputed
`len(batch_qubits)`/`len(batch_params)` on every operation for the CSR
offset arrays was itself a measurable cost (~0.02s of the ~0.34s total);
replacing that with running integer counters removed it.

**However, the overall conversion ratio barely moved**: ~15.2%
(post-index-caching) → ~15.8–16.7% (three-run average, post-`append_batch`,
statistically flat within run-to-run noise, **not** an improvement).
Profiling explains why: with the boundary-crossing cost eliminated, the
new dominant cost (~60% of `convert_circuit`'s time) is the per-instruction
Python loop body itself — principally Qiskit's own per-instruction
property access (`Instruction.is_parameterized()`, `Instruction.params`,
`ControlledGate.params`/`.name`, called ~100,000 times each, once per
operation, to read `qc.data`) — which is inherent to walking a
`QuantumCircuit`'s instruction stream, not something on MENOS's side of
the `menos._core` boundary. Reducing it further would mean changing which
Qiskit properties are read and when (e.g. skipping `is_parameterized()`
for some gates), trading off the unbound-parameter correctness check's
safety margin for speed. The owner reviewed this finding and chose to
accept the current ~16% ratio rather than pursue that trade-off — this
is now the closed, final position for Phase 2/3, not a deferred item.

**Honest accounting of "10% or optimize" from spec 2.7**: two
independent, profiling-justified optimizations were applied across two
phases (index caching, batched C++ transfer); both delivered a verified,
measured improvement in the specific cost they targeted; neither, nor
both together, brought the representative circuit's ratio under 10%,
because the circuit's total submission time is now dominated by
unavoidable Qiskit-side instruction-property-access cost. This is
reported as a permanent limitation (below), not silently accepted without
measurement.

## Limitations

- Only Python 3.14 is tested; `requires-python = ">=3.14"` reflects that,
  not Qiskit's broader 3.10–3.14 support.
- Multi-circuit `run_input` is rejected explicitly (`max_circuits = 1`),
  not batched.
- No noise, no observables, no async execution — all out of scope per
  `docs/BUILD_PLAN.md` Phase 2's "Not in this phase" list.
- Conversion is ~16% of total submission time for a 100,000-operation
  circuit, above the spec's 10% guideline, after two applied and verified
  optimizations (index caching, batched C++ transfer). The remaining cost
  is Qiskit-side per-instruction property access, not MENOS boundary
  overhead; the owner accepted this as a permanent limitation rather than
  trading away a correctness check for further speed. See "Conversion
  performance" above.
