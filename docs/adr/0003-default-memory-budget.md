# ADR 0003: Default memory budget fraction

## Status

Accepted.

## Context

`docs/BUILD_PLAN.md` Phase 1 (`src/plan`, `HardwareProfile`) requires: "The
default budget is a fixed fraction of total memory, recorded as a named
constant with a comment." It does not name the fraction. This is a
numerical threshold, not a fact derivable from the spec or from Qiskit --
per `CLAUDE.md`'s working agreement ("If the spec ... would force a guess
(mathematics, API behavior, thresholds), stop, state the conflict, and
propose options"), this was raised with the owner rather than picked
silently.

## Decision

`kDefaultMemoryBudgetFraction = 0.5` (`src/plan/hardware_profile.h`), used
whenever `JobSpec.memory_budget_bytes` is `std::nullopt`:

```
budget = memory_budget_bytes.value_or(total_memory_bytes * 0.5)
```

## Rationale

- 50% leaves headroom for the OS, other processes, and MENOS's own
  non-amplitude memory (the circuit, the plan, intermediate buffers),
  without being so conservative that ordinary-sized jobs are refused on a
  machine that could otherwise run them.
- A fraction rather than a fixed byte count, so the default scales with the
  machine actually running the job rather than being wrong on both a
  laptop and a large server.
- Always overridable per job via `RunOptions.memory_budget_bytes` /
  `JobSpec.memory_budget_bytes`, so this default only matters when the
  caller doesn't state a preference.

## Consequences

- On a machine with `total_memory_bytes` bytes, the largest exact
  dense-statevector job with no explicit budget is bounded by
  `n` such that `16 * 2^n <= 0.5 * total_memory_bytes`.
- Revisit if Phase 10's hardware calibration or real-world usage shows 50%
  is systematically wrong in either direction; that needs its own ADR
  update, not a silent change to the constant.
