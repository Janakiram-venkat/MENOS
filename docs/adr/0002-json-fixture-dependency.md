# ADR 0002: JSON dependency for the fixture-differential test

## Status

Accepted.

## Context

`docs/BUILD_PLAN.md` Phase 1, 1.6 requires ~100 Qiskit-generated circuits
with expected statevectors, "committed as JSON with the Qiskit version in
the header," loaded by "a test-only JSON dependency, pinned." A new
dependency needs an ADR per `CLAUDE.md`'s "minimal dependencies" prime
directive, even when it is test-only.

## Decision

**nlohmann/json**, tag `v3.12.0` (commit
`55f93686c01528224f448c19128836e7df245f72`, confirmed via the GitHub API),
pinned via `FetchContent` with `GIT_SHALLOW TRUE` (a non-shallow clone of
the full repository history was impractically slow in this environment;
a shallow clone of the tag completed in under a minute).

It is linked **only** into `menos_test_fixture_loader`
(`tests/ir/fixture_loader.{h,cpp}`), a test-support library, and only as
`PRIVATE` there, so the dependency never appears on the include path of
any `menos_*` production target and is never part of the shipped C++
library.

## Rationale

- Single-header, header-only, MIT-licensed, the de facto standard choice
  for JSON in modern C++ (widely used, actively maintained, no transitive
  dependencies of its own).
- Alternative considered and rejected: hand-rolling a minimal parser for
  this one fixture schema. Parsing correctness isn't what
  acceptance criterion 2 is testing — the statevector comparison is — so a
  hand-rolled parser would only add a second thing that could be subtly
  wrong, for no benefit over a well-tested library.
- Pinned by tag with the commit hash cross-checked via the GitHub API,
  matching `CLAUDE.md`'s "pin versions by tag or commit hash."

## Consequences

- `scripts/verify.sh`'s `dev`/`release` configure steps now fetch two
  dependencies (GoogleTest, nlohmann/json) instead of one. Both are
  test-only; the production library (`menos_ir`, `menos_engines_dense`,
  `menos_plan`, `menos_runtime`, `menos_api`) depends on neither.
