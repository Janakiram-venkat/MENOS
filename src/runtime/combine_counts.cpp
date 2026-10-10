#include "runtime/combine_counts.h"

#include "common/error.h"

namespace menos::runtime {

// bitstring_width (a clbit count, typically small) and shots (typically
// in the thousands) are not realistically confusable at any call site.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
std::map<BitString, std::uint64_t> combine_counts(
    std::span<const RegionCountsSampler* const> samplers,
    std::span<common::Rng> rngs,
    std::uint32_t bitstring_width,
    std::uint64_t shots,
    const ExecutionMonitor* monitor) {
    // NOLINTEND(bugprone-easily-swappable-parameters)
    common::ensure(samplers.size() == rngs.size(),
                   "combine_counts: samplers and rngs must have the same size");

    std::map<BitString, std::uint64_t> counts;
    // One safe boundary per shot (Phase 11, docs/adr/0015). Nothing is
    // committed to the caller until this returns, so abandoning here simply
    // discards a partial histogram. This is the multi-region counts path; the
    // sole-region one is monitored inside each engine.
    std::uint64_t boundaries = 0;
    for (std::uint64_t shot = 0; shot < shots; ++shot) {
        BitString combined(bitstring_width);
        for (std::size_t r = 0; r < samplers.size(); ++r) {
            combined |= samplers[r]->sample_one(rngs[r]);
        }
        counts[combined] += 1;
        ++boundaries;
        if (monitor != nullptr) {
            monitor->at_safe_boundary(boundaries);
        }
    }
    return counts;
}

}  // namespace menos::runtime
