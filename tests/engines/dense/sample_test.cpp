#include "engines/dense/sample.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>
#include <numeric>

namespace menos::engines::dense {
namespace {

using MeasureTarget = std::pair<std::uint32_t, std::uint32_t>;

// Test-only convenience: builds a BitString of the given width from a
// small unsigned integer literal, since BitString's own public API is
// deliberately bit-at-a-time (docs/BUILD_PLAN.md Phase 3, 3.4 gives no
// integer-literal construction requirement).
// width (a clbit count, typically small) and value (the bits to set) are
// not realistically confusable at any call site.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
BitString make_bitstring(std::uint32_t width, std::uint64_t value) {
    BitString bs(width);
    for (std::uint32_t bit = 0; bit < width; ++bit) {
        if ((value >> bit) & std::uint64_t{1}) {
            bs.set(bit, true);
        }
    }
    return bs;
}

StateVector make_plus_state() {
    StateVector state(1, /*budget_bytes=*/1'000'000);
    const double s = 1.0 / std::numbers::sqrt2;
    state.amplitudes()[0] = {s, 0.0};
    state.amplitudes()[1] = {s, 0.0};
    return state;
}

TEST(SampleTest, ZeroShotsProducesEmptyCounts) {
    const StateVector state = make_plus_state();
    const std::vector<MeasureTarget> targets{{0, 0}};
    const auto counts = sample(state, /*shots=*/0, /*seed=*/1, targets, /*bitstring_width=*/1);
    EXPECT_TRUE(counts.empty());
}

TEST(SampleTest, SameSeedGivesIdenticalCounts) {
    const StateVector state = make_plus_state();
    const std::vector<MeasureTarget> targets{{0, 0}};
    const auto counts_a =
        sample(state, /*shots=*/1000, /*seed=*/2024, targets, /*bitstring_width=*/1);
    const auto counts_b =
        sample(state, /*shots=*/1000, /*seed=*/2024, targets, /*bitstring_width=*/1);
    EXPECT_EQ(counts_a, counts_b);
}

TEST(SampleTest, TotalShotsAreConserved) {
    const StateVector state = make_plus_state();
    const std::vector<MeasureTarget> targets{{0, 0}};
    const auto counts = sample(state, /*shots=*/12345, /*seed=*/9, targets, /*bitstring_width=*/1);
    std::uint64_t total = 0;
    for (const auto& [key, count] : counts) {
        total += count;
    }
    EXPECT_EQ(total, 12345U);
}

TEST(SampleTest, MapsBasisBitsToClbitsThroughMeasureTargets) {
    // 2-qubit basis state |q1=1, q0=0> (index 2), measured q0->c1, q1->c0
    // (a deliberately swapped mapping): expect classical outcome bit 0
    // (from q1=1) and bit1=0 (from q0=0), i.e. outcome = 1.
    StateVector state(2, /*budget_bytes=*/1'000'000);
    for (auto& a : state.amplitudes()) {
        a = {0.0, 0.0};
    }
    state.amplitudes()[2] = {1.0, 0.0};
    const std::vector<MeasureTarget> targets{{0, 1}, {1, 0}};
    const auto counts = sample(state, /*shots=*/10, /*seed=*/1, targets, /*bitstring_width=*/2);
    ASSERT_EQ(counts.size(), 1U);
    EXPECT_EQ(counts.begin()->first, make_bitstring(2, 1));
    EXPECT_EQ(counts.begin()->second, 10U);
}

// Golden counts for one fixed seed (CLAUDE.md testing rules): documented
// as changing only if the RNG or the sampling algorithm changes.
TEST(SampleTest, GoldenCountsForFixedSeed) {
    const StateVector state = make_plus_state();
    const std::vector<MeasureTarget> targets{{0, 0}};
    const auto counts = sample(state, /*shots=*/20, /*seed=*/42, targets, /*bitstring_width=*/1);
    const std::map<BitString, std::uint64_t> expected{{make_bitstring(1, 0), 9},
                                                      {make_bitstring(1, 1), 11}};
    EXPECT_EQ(counts, expected);
}

// Chi-square against exact probabilities (CLAUDE.md testing rules):
// threshold is the 0.001-significance critical value of the chi-square
// distribution with 3 degrees of freedom (4 outcomes), computed
// independently from first principles (regularized incomplete gamma
// series), not tuned until this test passed.
TEST(SampleTest, ChiSquareAgainstExactProbabilities) {
    StateVector state(2, /*budget_bytes=*/1'000'000);
    const double p0 = 0.1, p1 = 0.2, p2 = 0.3, p3 = 0.4;
    state.amplitudes()[0] = {std::sqrt(p0), 0.0};
    state.amplitudes()[1] = {std::sqrt(p1), 0.0};
    state.amplitudes()[2] = {std::sqrt(p2), 0.0};
    state.amplitudes()[3] = {std::sqrt(p3), 0.0};

    const std::vector<MeasureTarget> targets{{0, 0}, {1, 1}};
    constexpr std::uint64_t shots = 100'000;
    const auto counts = sample(state, shots, /*seed=*/31415, targets, /*bitstring_width=*/2);

    const double expected[4] = {p0 * shots, p1 * shots, p2 * shots, p3 * shots};
    double chi_square = 0.0;
    for (std::uint64_t key = 0; key < 4; ++key) {
        const auto it = counts.find(make_bitstring(2, key));
        const double observed = (it == counts.end()) ? 0.0 : static_cast<double>(it->second);
        const double diff = observed - expected[key];
        chi_square += diff * diff / expected[key];
    }

    constexpr double kChiSquareCriticalDf3P0001 = 16.2662;  // see test file header comment
    EXPECT_LT(chi_square, kChiSquareCriticalDf3P0001)
        << "chi_square=" << chi_square << " (would reject the null hypothesis that "
        << "sample() matches the exact Born-rule probabilities)";
}

}  // namespace
}  // namespace menos::engines::dense
