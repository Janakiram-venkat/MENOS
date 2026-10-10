#pragma once

#include <cstdint>
#include <string>

namespace menos_benchmark {

// Everything docs/BUILD_PLAN.md Phase 3, 3.9 requires to reproduce a
// result: OS, architecture, compiler/build type, and CPU identification
// through a portable mechanism. Printed once by every benchmark binary's
// main() before Google Benchmark's own report.
[[nodiscard]] std::string describe_environment();

// A conservative per-allocation budget (the same
// kDefaultMemoryBudgetFraction of detected total memory the planner
// itself uses by default, see src/plan/hardware_profile.h) -- a benchmark
// checks a data point's required bytes against this and skips (never
// allocates blindly until the OS kills the process) rather than
// hard-coding a machine-specific qubit-count ceiling.
[[nodiscard]] std::uint64_t safe_allocation_budget_bytes();

}  // namespace menos_benchmark
