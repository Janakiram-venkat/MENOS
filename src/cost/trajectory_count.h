#pragma once

#include <cstdint>
#include <optional>

#include "menos/observable.h"

namespace menos::cost {

// How many trajectories a trajectory plan runs, and the observable bound that
// set it (Phase 13, docs/adr/0017 section 3).
struct TrajectoryCount {
    std::uint64_t trajectories;
    // C = sum of |coefficient| over the observable's non-identity terms, when
    // an expectation was requested.
    std::optional<double> coefficient_bound;
};

// N = max(shots, N_eps), where N_eps = max(2, ceil(C^2 / eps^2)) when an
// expectation is requested with precision eps, and 0 when it is not. Because
// every per-trajectory value X_t satisfies |X_t - c_0| <= C, Var(X_t) <= C^2,
// so N_eps guarantees a true standard error of at most eps before anything
// runs. N_eps is checked after rounding and raised if C / sqrt(N) still
// exceeds eps, so floating-point rounding can never weaken the guarantee.
//
// The single source of N: the planner computes it once, the trajectory
// engine's estimate() costs it, and the runtime runs exactly that many. Lives
// in the cost module rather than the engine so the planner can use it without
// naming a concrete engine (CLAUDE.md's architecture rule).
//
// Precondition: precision is set iff observable is non-null, and is finite and
// > 0 (src/api/menos.cpp validates it). Throws ResourceExceeded when N is not
// representable as a std::uint64_t.
[[nodiscard]] TrajectoryCount trajectory_count(std::uint64_t shots,
                                               const Observable* observable,
                                               std::optional<double> precision);

}  // namespace menos::cost
