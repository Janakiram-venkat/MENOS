#pragma once

#include <algorithm>
#include <map>
#include <optional>
#include <utility>

#include "menos/plan.h"

namespace menos::plan {

// What a run that was abandoned mid-flight taught the planner (Phase 11,
// docs/adr/0015).
//
// This is NOT a second cost model and NOT a recalibration. It carries one kind
// of fact, and that fact is a LOWER BOUND that was directly observed: engine E
// on backend B, on this exact job, has already spent this many seconds. The
// planner takes max(model estimate, observed floor), so an observation can only
// ever make a candidate look more expensive, never cheaper. A wrong observation
// cannot make the planner optimistic.
//
// Why a floor rather than a measured rate. The obvious alternative -- divide
// observed seconds by completed work units to get a per-engine rate, then
// re-estimate -- was measured and rejected. The MPS engine's seconds per work
// unit swings 30x WITHIN a single run as its bond dimension grows, so a rate
// taken from partial progress projects the total at 0.16x to 0.40x of the truth
// (docs/adr/0015). A projection built on it would under-predict exactly where
// the planner is already wrong. Elapsed-so-far needs no such assumption: it is
// true by construction, and it is monotone, which is what makes the replan loop
// terminate.
//
// Scope, stated rather than left to be discovered: a floor is attributed to one
// (engine, backend) pair, so it applies to the single-engine Direct* candidates
// only. A Factorized or CutAndRecombine candidate spans several engines and
// this phase does not attribute elapsed time per region, so no floor is applied
// to it. Both of those models were measured accurate to within 0.70x-1.33x, so
// they are not where the problem is.
struct RuntimeObservations {
    std::map<std::pair<Engine, Backend>, double> observed_floor_seconds;

    // The floor for this pair, or nullopt if this pair was never observed.
    [[nodiscard]] std::optional<double> floor_for(Engine engine, Backend backend) const {
        const auto it = observed_floor_seconds.find({engine, backend});
        if (it == observed_floor_seconds.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    // Records that (engine, backend) has already spent `seconds` on this job.
    // Keeps the LARGER of any existing floor and this one, so accumulating
    // observations across successive abandoned attempts can never walk a floor
    // backwards.
    void observe(Engine engine, Backend backend, double seconds) {
        double& floor = observed_floor_seconds[{engine, backend}];
        floor = std::max(floor, seconds);
    }

    [[nodiscard]] bool empty() const {
        return observed_floor_seconds.empty();
    }
};

}  // namespace menos::plan
