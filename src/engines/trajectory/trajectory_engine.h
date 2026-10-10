#pragma once

#include <cstdint>

#include "menos/simulation_engine.h"

namespace menos::engines::trajectory {

// The trajectory count N is fixed by cost::trajectory_count
// (src/cost/trajectory_count.h) and arrives in request.trajectories.

// The seed of trajectory t: the (t + 1)-th output of
// common::SplitMix64(base_seed), computed directly from t rather than by
// generating the t outputs before it (docs/adr/0017 section 4). So trajectory
// t's stream does not depend on how many trajectories ran before it.
[[nodiscard]] std::uint64_t trajectory_seed(std::uint64_t base_seed, std::uint64_t t);

// Monte-Carlo trajectory simulation of a noisy circuit (Phase 13,
// docs/adr/0017 section 2). Each of request.trajectories runs is one pure
// state, in which every channel is replaced by one of its Kraus operators,
// drawn with its exact probability ||K_i psi||^2 and then renormalized.
// Averaging over trajectories reproduces the channel exactly, for every CPTP
// channel, so the results are unbiased but statistical: Result::exact() is
// false for them.
//
// Outputs: an expectation value as the per-term mean over trajectories, plus
// its measured standard error; and counts, one shot per trajectory for the
// first `shots` trajectories, with readout errors applied as the density-matrix
// engine applies them. No statevector (a noisy job has none). CPU only, and not
// used as a Factorized region engine in Phase 13 (docs/adr/0017 section 5).
//
// Sole-region only: execute() refuses a request that is not.
class TrajectoryEngine : public SimulationEngine {
public:
    [[nodiscard]] std::string_view name() const override;
    [[nodiscard]] Engine engine_id() const override;
    [[nodiscard]] Backend backend() const override;
    [[nodiscard]] EngineCapabilities capabilities() const override;
    [[nodiscard]] CostEstimate estimate(const EngineEstimateRequest& request) const override;
    [[nodiscard]] EngineExecutionResult execute(
        const EngineExecutionRequest& request) const override;
};

}  // namespace menos::engines::trajectory
