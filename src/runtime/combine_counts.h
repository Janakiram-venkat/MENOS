#pragma once

#include <cstdint>
#include <map>
#include <span>

#include "common/rng.h"
#include "menos/bitstring.h"
#include "menos/simulation_engine.h"

namespace menos::runtime {

// Exact multi-region counts combination (docs/BUILD_PLAN.md Phase 3,
// 3.4). For each of `shots` trials, draws one region-local outcome per
// region from that region's own RegionCountsSampler (each engine's own
// implementation, dense or stabilizer -- Phase 4 generalizes this from a
// concrete engines::dense::CumulativeDistribution to the engine-agnostic
// interface), using that region's own common::Rng stream (one
// independent stream per region, seeded via a per-component sub-seed
// derived from common::SplitMix64), and OR-combines the per-region
// outcomes (valid: each region's outcome occupies disjoint global
// classical-bit positions by construction). This is exact because, for a
// fixed shot index, every region's draw comes from an independent RNG
// stream and is not a sorted order statistic -- there is no rank-pairing
// between regions to introduce correlation. See docs/adr/0005 for the
// counterexample that rejected an earlier "sort each region, then merge
// by cumulative-count boundary" draft, which was statistically biased.
//
// samplers.size() must equal rngs.size(); every sampler must have been
// built with the same bitstring_width.
[[nodiscard]] std::map<BitString, std::uint64_t> combine_counts(
    std::span<const RegionCountsSampler* const> samplers,
    std::span<common::Rng> rngs,
    std::uint32_t bitstring_width,
    std::uint64_t shots,
    const ExecutionMonitor* monitor = nullptr);

}  // namespace menos::runtime
