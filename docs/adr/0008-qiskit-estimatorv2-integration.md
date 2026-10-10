# ADR 0008: Qiskit `EstimatorV2` integration

## Status

Accepted.

## Context

`docs/BUILD_PLAN.md` Phase 5 requires `menos.qiskit` to gain an estimator
implementing the installed version's `BaseEstimatorV2` contract, verified
against the installed source before writing the adapter (the same
anti-hallucination discipline `docs/adr/0004` used for `BackendV2`).

Everything below was read directly from **installed Qiskit 2.5.2 source**
in this session's `.venv` (Python 3.14.4), or confirmed by running code
against the installed package -- not recalled from before this
conversation's context compaction, even though an earlier pass of this
same session had already investigated the same APIs (the investigation
was redone from scratch this time, per CLAUDE.md's own rule).

### `BaseEstimatorV2` (`qiskit.primitives.base`)

An ABC with exactly one abstract method:

```python
def run(self, pubs: Iterable[EstimatorPubLike], *, precision: float | None = None
       ) -> BasePrimitiveJob[PrimitiveResult[PubResult]]
```

### `StatevectorEstimator` (the installed reference implementation), read in full

```python
def run(self, pubs, *, precision=None):
    if precision is None:
        precision = self._default_precision
    coerced_pubs = [EstimatorPub.coerce(pub, precision) for pub in pubs]
    job = PrimitiveJob(self._run, coerced_pubs)
    job._submit()
    return job

def _run(self, pubs):
    return PrimitiveResult([self._run_pub(pub) for pub in pubs], metadata={"version": 2})

def _run_pub(self, pub):
    ...
    paulis, coeffs = zip(*observable.items())
    obs = SparsePauliOp(paulis, coeffs)
    expectation_value = np.real_if_close(final_state.expectation_value(obs))
    ...
    evs[index] = expectation_value
    data = DataBin(evs=evs, stds=stds, shape=evs.shape)
    return PubResult(data, metadata={"target_precision": precision,
                                     "circuit_metadata": pub.circuit.metadata})
```

`default_precision=0.0` is `StatevectorEstimator`'s own default.

### `EstimatorPub.coerce`/`.validate()`

`coerce(pub, precision)` rejects a bare `QuantumCircuit` pub with a
message telling the caller to wrap it in `[]`; accepts a 2/3/4-tuple
`(circuit, observables[, parameter_values[, precision]])`; builds
`observables` via `ObservablesArray.coerce(pub[1])`; builds
`parameter_values` via `BindingsArray.coerce(...)` only when a 3rd
element was given and is not `None` (otherwise `parameter_values` stays
`None` on the constructed pub, since `EstimatorPub`'s own dataclass
default handles the "no values" case downstream); calls `.validate()`
when constructed from a raw tuple.

`.validate()` cross-checks `self.parameter_values.num_parameters !=
self.circuit.num_parameters` and raises `ValueError` **synchronously,
inside `coerce()`, before a job is even created** -- confirmed by direct
construction: `EstimatorPub.coerce((qc_with_1_param, obs), precision=0.0)`
for a `qc` with one unbound `Parameter` and no third pub element raises
`ValueError: The number of values (0) does not match the number of
parameters (1) for the circuit.` immediately. This means `MENOSEstimator`
never needs to reject that specific "no bindings provided for a
parameterized circuit" case itself -- Qiskit's own pub validation already
refuses it, before `_run_pub` is ever reached.

`MENOSEstimator._run_pub` still checks `circuit.num_parameters > 0`
directly and raises `UnsupportedFeatureError`, because that check is
reachable for the *other* case: a pub with parameter **values matching
the circuit's parameter count** (`EstimatorPub.coerce`/`.validate()`
pass cleanly), but MENOS has no runtime parameter-binding mechanism at
all this phase -- `pub.circuit` is still the unbound circuit either way
(Qiskit's pub structure carries the circuit and its parameter bindings
as two separate fields, never a pre-bound circuit). Both paths are
exercised by `python/tests/test_estimator.py`, not just one assumed to
cover the whole "parameterized circuit" surface.

### `pub.observables[index]`: a plain `dict[str, complex]`

Confirmed by direct construction: for `pub = EstimatorPub.coerce((qc,
SparsePauliOp(["ZZ","XX"], [1.0,0.5])), precision=0.0)`,
`pub.observables` prints as `ObservablesArray({'ZZ': 1.0, 'XX': 0.5},
shape=())`, and indexing it (`pub.observables[()]`) returns exactly that
plain `dict`. This is exactly what the reference `_run_pub` consumes via
`observable.items()`. `pub.observables.shape` is `()` for a single
observable and `(k,)`/`(m,n)`/etc. for an array of observables (confirmed
for a 2-element list, `shape=(2,)`).

### `SparsePauliOp.paulis.x`/`.z`/`.phase`: exactly MENOS's own convention

Confirmed by direct construction and print, for `"Y"`, `"IY"`, `"YI"`,
`"XY"`, `"YX"`, `"ZY"`, `"YZY"`: `.paulis.phase` is `0` in every case;
`.paulis.x[term][qubit]`/`.paulis.z[term][qubit]` are boolean arrays
indexed **by qubit number**, not by string position (confirmed: for
label `"XY"` on 2 qubits, `x=[True,True]`, `z=[True,False]` — qubit 0 is
`Y` (`x=True,z=True`), qubit 1 is `X` (`x=True,z=False`), matching
Qiskit's own leftmost-character-is-highest-qubit label convention
translated automatically by the `.x`/`.z` accessors). Any `Y = i·X·Z`
phase is already folded into `.coeffs`, never left in `.paulis.phase`
(confirmed: coefficient `1.0` in, `1.0+0j` out for every case above, not
`i` or `-i`). This is **exactly** `docs/adr/0006`/`0007`'s
`common::PauliRow` convention, so `_observable_convert.py::
convert_observable` reads `.x`/`.z`/`.coeffs` directly, with no Pauli
label string parsed or emitted anywhere in the conversion.

Also confirmed: `Pauli((z_bool_array, x_bool_array))` constructs a
`Pauli` directly from boolean arrays with no label string at all,
round-tripping `.x`/`.z` exactly and giving the correct
`Statevector.expectation_value` — used by `tools/gen_fixtures.py`'s
random-observable generator (decision 5 below) to sidestep the label
convention entirely on the fixture-generation side too.

### `PrimitiveJob`, `PrimitiveResult`, `PubResult`, `DataBin`, `EstimatorPub`

All import cleanly from the `qiskit.primitives` top-level package.
`PrimitiveJob(function, *args, **kwargs)` + `job._submit()` runs
`function(*args, **kwargs)` on a one-worker `ThreadPoolExecutor` and
stores the future; `job.result()` blocks on it. `PrimitiveResult(pub_results,
metadata=None)`, `PubResult(data: DataBin, metadata=None)`,
`DataBin(**kwargs)` (arbitrary named arrays, `evs`/`stds`/`shape` by the
reference implementation's own convention, not an enforced schema).

## Decisions

1. **`evs` dtype is `float64`; a non-Hermitian observable's imaginary
   part is dropped, exactly like the reference estimator.**
   `StatevectorEstimator` computes `np.real_if_close(expectation_value)`
   into a `float64`-typed array, which silently discards any
   non-negligible imaginary part on assignment. `docs/BUILD_PLAN.md`
   already decided MENOS "does not validate Hermiticity, matching
   `SparsePauliOp`." `MENOSEstimator` takes `.real` of MENOS's own
   `complex` `expectation_value` directly into `evs[index]` -- mirroring
   the *installed reference implementation's actual behavior*, not
   inventing a new validation rule MENOS's own C++ layer doesn't have
   either.
2. **No parameter binding.** `MENOSEstimator` rejects any pub whose
   `circuit.num_parameters > 0` with `UnsupportedFeatureError` inside
   `_run_pub` -- reachable exactly when `EstimatorPub`'s own validation
   doesn't already refuse the pub first (see "no bindings provided" vs.
   "bindings provided but MENOS still can't bind them" above). This
   matches `convert.py`'s existing unbound-`Parameter` rejection and
   the original session's stated decision that MENOS has no
   parameter-binding concept.
3. **`precision` is recorded, never used to add noise.** All three MENOS
   engines are exact this phase (`docs/BUILD_PLAN.md`: "this phase's
   engines are all exact, so the estimator always returns the analytic
   value"). `precision` (default `0.0`, matching `StatevectorEstimator`)
   is validated by `EstimatorPub.coerce` (already enforces non-negative)
   and stored in each `PubResult`'s metadata as `target_precision`,
   exactly like the reference, but never perturbs the returned value --
   `docs/BUILD_PLAN.md` explicitly permits recording `precision=0.0`
   "rather than guessing at shot-noise semantics."
4. **Qiskit's own job/result machinery is reused directly.**
   `PrimitiveJob`/`PrimitiveResult`/`PubResult`/`DataBin`/`EstimatorPub`
   are used unmodified -- no new Job class, mirroring `MENOSJob`'s own
   precedent of reusing `JobV1` rather than reinventing synchronous-job
   semantics.
5. **One conversion function, `_observable_convert.py::
   convert_observable(op: SparsePauliOp, num_qubits) -> Observable`**,
   reused by both `estimator.py` (which first builds a `SparsePauliOp`
   from each pub observable's `dict[str, complex]`, exactly like the
   reference `_run_pub` does) and, independently, by
   `tools/gen_fixtures.py`'s own (necessarily separate, since
   `gen_fixtures.py` cannot depend on the compiled `menos` package) but
   conceptually identical `.x`/`.z`-reading logic for emitting the
   fixture JSON's `x_qubits`/`z_qubits` term fields.
6. **The circuit is converted once per pub, not once per observable.**
   `convert.py::convert_circuit` (reused, not duplicated) is called a
   single time per pub, hoisted out of the per-observable loop -- the
   circuit does not change across a pub's observable array, and MENOS
   has no batching API this phase, so each observable still becomes its
   own `menos._core.run()` call, but circuit conversion itself is not
   repeated.

## Not in this phase

Shot-based (sampling) expectation estimation, non-Pauli (matrix)
observables, parameterized circuits -- all per `docs/BUILD_PLAN.md`
Phase 5's existing "Not in this phase" list; nothing here expands it.

## Validation

`python/tests/test_estimator.py`: one circuit routed to each of dense,
stabilizer and Pauli propagation, each compared against the equivalent
direct `menos._core.run(...).expectation_value` call (the C++ path is
the oracle for this criterion, matching its literal wording); an
`EstimatorPub`-shape test (multiple observables in one pub); both
parameterized-circuit rejection paths documented above.
