#pragma once

#include <cstdint>
#include <random>

namespace menos::common {

// std::mt19937_64 seeded from the job seed. Raw 64-bit output is converted
// to a double in [0, 1) with our own conversion, not
// std::uniform_real_distribution (implementation-defined -- CLAUDE.md's
// determinism invariant). Shared by the dense engine (sampling and its
// per-region CumulativeDistribution) and the stabilizer engine (per-shot
// affine-subspace generator selection, docs/BUILD_PLAN.md Phase 4) --
// two independent consumers of the same seeded-word-stream family, not a
// second, unvalidated PRNG.
class Rng {
public:
    explicit Rng(std::uint64_t seed);

    [[nodiscard]] double next_uniform();

    // The raw 64-bit engine output, for a consumer that needs its own
    // bits directly (e.g. RegionCountsSampler::sample_one's
    // random_word) rather than a uniform double.
    [[nodiscard]] std::uint64_t next_word();

    // The same conversion next_uniform() applies internally, exposed so
    // a word drawn elsewhere (e.g. already passed across the
    // RegionCountsSampler interface) maps to [0, 1) identically.
    [[nodiscard]] static double word_to_uniform(std::uint64_t word);

private:
    std::mt19937_64 engine_;
};

}  // namespace menos::common
