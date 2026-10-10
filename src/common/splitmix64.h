#pragma once

#include <cstdint>

namespace menos::common {

// Sebastiano Vigna's splitmix64 (public domain,
// https://prng.di.unimi.it/splitmix64.c). Used to derive deterministic,
// well-distributed per-component sub-seeds from a single job seed
// (docs/BUILD_PLAN.md Phase 3, 3.4: "deterministic sub-seeds derived
// from... splitmix64... from (job seed, component index)"). Not used as
// the sampling RNG itself -- common::Rng (std::mt19937_64) keeps that
// role; SplitMix64 only produces the per-component seeds that feed each
// region's own Rng.
class SplitMix64 {
public:
    explicit SplitMix64(std::uint64_t seed) : state_(seed) {}

    [[nodiscard]] std::uint64_t next() {
        state_ += 0x9E3779B97F4A7C15ULL;
        std::uint64_t z = state_;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

private:
    std::uint64_t state_;
};

}  // namespace menos::common
