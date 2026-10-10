#include "common/splitmix64.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

namespace menos::common {
namespace {

TEST(SplitMix64Test, SameSeedProducesSameStream) {
    SplitMix64 a(12345);
    SplitMix64 b(12345);
    for (int i = 0; i < 1000; ++i) {
        EXPECT_EQ(a.next(), b.next());
    }
}

TEST(SplitMix64Test, DifferentSeedsProduceDifferentFirstValues) {
    // Not a proof of quality, just a smoke check that distinct seeds
    // don't collide trivially, across a handful of seeds.
    std::array<std::uint64_t, 8> firsts{};
    for (std::uint64_t seed = 0; seed < firsts.size(); ++seed) {
        SplitMix64 mixer(seed);
        firsts[seed] = mixer.next();
    }
    for (std::size_t i = 0; i < firsts.size(); ++i) {
        for (std::size_t j = i + 1; j < firsts.size(); ++j) {
            EXPECT_NE(firsts[i], firsts[j]) << "seeds " << i << " and " << j;
        }
    }
}

TEST(SplitMix64Test, SuccessiveValuesFromOneStreamAreDistinct) {
    SplitMix64 mixer(777);
    std::uint64_t previous = mixer.next();
    for (int i = 0; i < 999; ++i) {
        const std::uint64_t current = mixer.next();
        EXPECT_NE(current, previous);
        previous = current;
    }
}

// Chi-square uniformity test on the top 4 bits of next()'s output (16
// exactly-equal-width bins across [0, 2^64), no division/modulo bias
// since 2^64 is exactly divisible by 16), against the 0.001-significance
// critical value for a chi-square distribution with 15 degrees of freedom
// (16 bins), independently verified this session via
// scipy.stats.chi2.ppf(0.999, 15) = 37.69729821835383 -- not recalled from
// memory (CLAUDE.md: never invent/misremember a statistical threshold).
// This is a property test (avalanche/uniformity), not a check against a
// specific "expected value" literal, per docs/BUILD_PLAN.md 3.0's caution
// about splitmix64 not already existing in this codebase.
TEST(SplitMix64Test, OutputIsUniformlyDistributedAcrossSixteenBins) {
    constexpr int kBins = 16;
    constexpr std::uint64_t kDraws = 200'000;
    std::array<std::uint64_t, kBins> observed{};

    SplitMix64 mixer(31415);
    for (std::uint64_t i = 0; i < kDraws; ++i) {
        const std::uint64_t value = mixer.next();
        const auto bin = static_cast<std::size_t>(value >> 60);  // top 4 bits, exactly kBins values
        observed[bin]++;
    }

    const double expected_per_bin = static_cast<double>(kDraws) / kBins;
    double chi_square = 0.0;
    for (const std::uint64_t count : observed) {
        const double diff = static_cast<double>(count) - expected_per_bin;
        chi_square += diff * diff / expected_per_bin;
    }

    constexpr double kChiSquareCriticalDf15P0001 = 37.69729821835383;
    EXPECT_LT(chi_square, kChiSquareCriticalDf15P0001)
        << "chi_square=" << chi_square
        << " (would reject the null hypothesis that splitmix64's output is uniform)";
}

}  // namespace
}  // namespace menos::common
