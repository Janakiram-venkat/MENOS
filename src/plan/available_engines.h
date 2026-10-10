#pragma once

#include <vector>

#include "menos/simulation_engine.h"

namespace menos::plan {

// The only place that #includes both concrete engine headers
// (docs/BUILD_PLAN.md Phase 4: "the planner sees engines only through
// their capability and cost interface, never through implementations").
// Fixed order: dense first, so a factorized region's cost-tie-break
// ("ties go to dense") falls out of pick_cheapest_engine's own
// earliest-index rule without any special-casing.
[[nodiscard]] const std::vector<const SimulationEngine*>& available_engines();

// available_engines() followed by the density-matrix engine: the engine set
// for a job that contains noise (Phase 12, docs/adr/0016 section 5). A
// noiseless job never sees the density-matrix engine, so its candidates,
// Factorized region choices and every infeasibility reason are exactly what
// they were before Phase 12. It is last, so a pure region of a noisy job still
// takes its cheapest pure-state engine, and ties go to them.
[[nodiscard]] const std::vector<const SimulationEngine*>& noisy_job_engines();

// The trajectory engine (Phase 13, docs/adr/0017). Deliberately NOT in either
// list above: it is planned only as the DirectTrajectory candidate of a noisy
// job whose caller opted in, never as a Factorized region engine (combining
// independent statistical region estimates needs an error-propagation rule
// Phase 13 does not define, docs/adr/0017 section 5). Keeping it out of the
// lists is what keeps every Factorized plan exactly as it was.
[[nodiscard]] const SimulationEngine& trajectory_engine();

}  // namespace menos::plan
