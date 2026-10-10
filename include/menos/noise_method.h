#pragma once

namespace menos {

// How a noisy job may be simulated (Phase 13, docs/adr/0017 section 9, Q1).
//
// Exact (the default): only the exact density-matrix engine, as in Phase 12.
// Auto: the planner may also run a noisy counts request by trajectory sampling,
// chosen by cost like any other candidate. Trajectory counts are independent
// samples of the noisy distribution, and the Result is flagged not exact.
//
// Has no effect on a noiseless job. An expectation request opts in separately,
// through RunOptions::expectation_precision.
enum class NoiseMethod { Exact, Auto };

}  // namespace menos
