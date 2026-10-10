#include "runtime/combine_counts.h"

#include <gtest/gtest.h>

#include <numbers>

#include "engines/dense/cumulative_distribution.h"
#include "engines/dense/state_vector.h"

namespace menos::runtime {
namespace {

using common::Rng;
using engines::dense::CumulativeDistribution;
using engines::dense::StateVector;
using MeasureTarget = std::pair<std::uint32_t, std::uint32_t>;

StateVector make_plus_state() {
    StateVector state(1, /*budget_bytes=*/1'000'000);
    const double s = 1.0 / std::numbers::sqrt2;
    state.amplitudes()[0] = {s, 0.0};
    state.amplitudes()[1] = {s, 0.0};
    return state;
}

std::vector<const RegionCountsSampler*> as_samplers(const std::vector<CumulativeDistribution>& d) {
    std::vector<const RegionCountsSampler*> samplers;
    samplers.reserve(d.size());
    for (const CumulativeDistribution& dist : d) {
        samplers.push_back(&dist);
    }
    return samplers;
}

TEST(CombineCountsTest, TotalShotsAreConserved) {
    const StateVector region_a = make_plus_state();
    const StateVector region_b = make_plus_state();
    const std::vector<MeasureTarget> targets_a{{0, 0}};
    const std::vector<MeasureTarget> targets_b{{0, 1}};
    const std::vector<CumulativeDistribution> distributions{
        CumulativeDistribution(region_a, targets_a, /*bitstring_width=*/2),
        CumulativeDistribution(region_b, targets_b, /*bitstring_width=*/2)};
    std::vector<Rng> rngs{Rng(1), Rng(2)};

    const auto counts =
        combine_counts(as_samplers(distributions), rngs, /*bitstring_width=*/2, /*shots=*/12345);
    std::uint64_t total = 0;
    for (const auto& [key, count] : counts) {
        total += count;
    }
    EXPECT_EQ(total, 12345U);
}

TEST(CombineCountsTest, SameSeedsGiveIdenticalCombinedCounts) {
    const StateVector region_a = make_plus_state();
    const StateVector region_b = make_plus_state();
    const std::vector<MeasureTarget> targets_a{{0, 0}};
    const std::vector<MeasureTarget> targets_b{{0, 1}};

    const std::vector<CumulativeDistribution> distributions{
        CumulativeDistribution(region_a, targets_a, 2),
        CumulativeDistribution(region_b, targets_b, 2)};
    std::vector<Rng> rngs_1{Rng(42), Rng(43)};
    std::vector<Rng> rngs_2{Rng(42), Rng(43)};

    const auto counts_1 = combine_counts(as_samplers(distributions), rngs_1, 2, 1000);
    const auto counts_2 = combine_counts(as_samplers(distributions), rngs_2, 2, 1000);
    EXPECT_EQ(counts_1, counts_2);
}

// The statistical-equivalence property docs/BUILD_PLAN.md Phase 3, 3.4
// requires, and the exact property whose absence (in a rejected earlier
// draft that paired two independently rank-sorted per-region sequences by
// shared shot index) was caught by hand-deriving a biased 2-shot/2-region
// counterexample this session (see docs/adr/0005). Two independent |+>
// regions, each measured to a different clbit: the combined outcome must
// be uniform over all 4 two-bit combinations, not biased toward
// same-region-outcome pairs.
TEST(CombineCountsTest, TwoIndependentRegionsProduceUniformJointDistribution) {
    const StateVector region_a = make_plus_state();
    const StateVector region_b = make_plus_state();
    const std::vector<MeasureTarget> targets_a{{0, 0}};
    const std::vector<MeasureTarget> targets_b{{0, 1}};
    const std::vector<CumulativeDistribution> distributions{
        CumulativeDistribution(region_a, targets_a, 2),
        CumulativeDistribution(region_b, targets_b, 2)};
    std::vector<Rng> rngs{Rng(2024), Rng(9999)};

    constexpr std::uint64_t shots = 200'000;
    const auto counts = combine_counts(as_samplers(distributions), rngs, 2, shots);

    const double expected_each = 0.25 * static_cast<double>(shots);
    double chi_square = 0.0;
    for (std::uint64_t value = 0; value < 4; ++value) {
        BitString key(2);
        key.set(0, (value & 1) != 0);
        key.set(1, (value & 2) != 0);
        const auto it = counts.find(key);
        const double observed = (it == counts.end()) ? 0.0 : static_cast<double>(it->second);
        const double diff = observed - expected_each;
        chi_square += diff * diff / expected_each;
    }

    // df=3, p=0.001 critical value 16.2662 (same constant independently
    // re-verified this session via scipy.stats.chi2.ppf(0.999, 3), used
    // consistently across this codebase's chi-square tests).
    constexpr double kChiSquareCriticalDf3P0001 = 16.2662;
    EXPECT_LT(chi_square, kChiSquareCriticalDf3P0001)
        << "chi_square=" << chi_square
        << " (would reject the null hypothesis that combine_counts produces an unbiased joint "
           "sample -- this is exactly the failure mode of the rejected sorted-merge draft, see "
           "docs/adr/0005)";
}

TEST(CombineCountsTest, RegionWithNoSamplersProducesAllZeroOutcome) {
    // The idle-region case: zero samplers/rngs participate, every shot's
    // combined outcome is the all-zero BitString.
    const std::vector<const RegionCountsSampler*> samplers;
    std::vector<Rng> rngs;
    const auto counts = combine_counts(samplers, rngs, /*bitstring_width=*/3, /*shots=*/50);
    ASSERT_EQ(counts.size(), 1U);
    EXPECT_EQ(counts.begin()->first, BitString(3));
    EXPECT_EQ(counts.begin()->second, 50U);
}

}  // namespace
}  // namespace menos::runtime
