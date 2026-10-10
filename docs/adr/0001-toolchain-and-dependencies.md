# ADR 0001: Toolchain and dependencies

## Status

Accepted.

## Context

Phase 0 (`docs/BUILD_PLAN.md` §6) requires this ADR to record the
compilers and minimum versions, platform support, and dependency pins
`scripts/verify.sh` builds against.

## Compilers and minimum versions

Per `CLAUDE.md`: GCC 12+, Clang 16+, AppleClang 15+.

Detected and used in this session, on the machine that ran Phase 0:

| Tool | Version detected |
|---|---|
| GCC (`gcc`/`g++`) | 15.2.0 (Ubuntu 15.2.0-16ubuntu1) |
| Clang / clang-format / clang-tidy | 21.1.6 (installed from the Ubuntu `resolute` universe repository) |
| CMake | 4.2.3 |

Both compilers exceed the stated minimums.

## Platforms

Per `CLAUDE.md`: Linux (x86_64, aarch64) and macOS (arm64). Windows is not
supported.

`scripts/verify.sh` was run in this session only on **Linux x86_64**
(Ubuntu, kernel 7.0.0-31-generic). **macOS arm64 has not been verified** —
no such machine was available in this session. This is a known gap, not a
silent claim of cross-platform support; verify on macOS before relying on
it there.

## Dependencies

### GoogleTest

- **Pin:** tag `v1.18.0`, commit `063de7e9578f82b369302001269680b4b1553359`
  (confirmed against the GitHub API for `google/googletest`, published
  2026-08-10).
- **Mechanism:** `FetchContent` in the root `CMakeLists.txt`, pinned by
  commit hash (`GIT_TAG`), per `CLAUDE.md`'s "pin versions by tag or commit
  hash."
- **Why this version:** it is the current latest stable release. GoogleTest
  1.18.x requires C++17 minimum, which is satisfied by this project's
  C++20 standard.
- **Why FetchContent and not a system package:** the version available via
  system package managers varies by platform and is not independently
  pinned; `FetchContent` gives every developer and CI runner the exact same
  commit regardless of OS, matching "Determinism" in `CLAUDE.md`.

No other dependency is introduced in Phase 0.

### OpenMP (amendment, Phase 15, 2026-10-08) — WITHDRAWN the same day

OpenMP was added as a required dependency for multi-threaded dense gate application (`docs/adr/0019` section 3.1, Q2) and withdrawn the same day. Its default waiting policy stalled every gate by 5–30 ms on the gate machine, and only an environment variable read at load time could change that. MENOS now uses its own thread pool (`src/kernels/cpu/thread_pool.h`), which needs only the platform thread library: `find_package(Threads REQUIRED)`, linked to `menos_kernels_cpu`. That is part of every supported toolchain (pthreads on Linux and macOS), so no dependency is added, fetched or pinned.

## Build generator

The default CMake generator (Unix Makefiles) is used rather than Ninja.
Phase 0 does not require build speed, and this avoids adding a dependency
(`ninja-build`) the phase spec does not ask for. This can be revisited with
its own ADR if build times become a problem.
