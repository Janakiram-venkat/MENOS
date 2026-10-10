#include "common/rng.h"

#include <gtest/gtest.h>

#include <random>

namespace menos::common {
namespace {

TEST(RngTest, ConversionFormulaMatchesSpec) {
    // Independent oracle: a freshly seeded std::mt19937_64, not Rng's own
    // internals, re-deriving CLAUDE.md's "(x >> 11) * 2^-53" formula.
    constexpr std::uint64_t seed = 12345;
    std::mt19937_64 reference(seed);
    const std::uint64_t raw = reference();
    const double expected = static_cast<double>(raw >> 11) * (1.0 / 9007199254740992.0);

    Rng rng(seed);
    EXPECT_EQ(rng.next_uniform(), expected);
}

TEST(RngTest, SameSeedProducesSameSequence) {
    Rng a(777);
    Rng b(777);
    for (int i = 0; i < 100; ++i) {
        EXPECT_EQ(a.next_uniform(), b.next_uniform());
    }
}

TEST(RngTest, DifferentSeedsProduceDifferentSequences) {
    Rng a(1);
    Rng b(2);
    bool any_different = false;
    for (int i = 0; i < 10; ++i) {
        if (a.next_uniform() != b.next_uniform()) {
            any_different = true;
        }
    }
    EXPECT_TRUE(any_different);
}

TEST(RngTest, ValuesStayInUnitInterval) {
    Rng rng(42);
    for (int i = 0; i < 10'000; ++i) {
        const double u = rng.next_uniform();
        EXPECT_GE(u, 0.0);
        EXPECT_LT(u, 1.0);
    }
}

// next_word() returns the same raw engine output next_uniform() derives
// its conversion from -- verified directly against a freshly seeded
// std::mt19937_64, not Rng's own internals.
TEST(RngTest, NextWordMatchesRawEngineOutput) {
    constexpr std::uint64_t seed = 2024;
    std::mt19937_64 reference(seed);
    const std::uint64_t expected = reference();

    Rng rng(seed);
    EXPECT_EQ(rng.next_word(), expected);
}

TEST(RngTest, NextWordAndNextUniformShareOneAdvancingStream) {
    // Interleaving next_word() and next_uniform() must advance the same
    // underlying std::mt19937_64 exactly once per call, in call order --
    // not two independent sub-streams.
    Rng rng(7);
    std::mt19937_64 reference(7);
    EXPECT_EQ(rng.next_word(), reference());
    EXPECT_EQ(rng.next_uniform(), Rng::word_to_uniform(reference()));
    EXPECT_EQ(rng.next_word(), reference());
}

TEST(RngTest, WordToUniformMatchesNextUniformsOwnConversion) {
    constexpr std::uint64_t kWord = 0x0123'4567'89ab'cdefULL;
    const double expected = static_cast<double>(kWord >> 11) * (1.0 / 9007199254740992.0);
    EXPECT_EQ(Rng::word_to_uniform(kWord), expected);
}

}  // namespace
}  // namespace menos::common
