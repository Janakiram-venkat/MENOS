#include "common/checked_math.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

#include "common/error.h"

namespace menos::common {
namespace {

constexpr std::uint64_t kMax = std::numeric_limits<std::uint64_t>::max();

TEST(CheckedAddTest, AddsNormally) {
    EXPECT_EQ(checked_add(2, 3), 5U);
    EXPECT_EQ(checked_add(0, 0), 0U);
}

TEST(CheckedAddTest, AllowsResultAtMax) {
    EXPECT_EQ(checked_add(kMax, 0), kMax);
    EXPECT_EQ(checked_add(kMax - 1, 1), kMax);
}

TEST(CheckedAddTest, ThrowsResourceExceededOnOverflow) {
    // The [[nodiscard]] result is genuinely unused here: the test only
    // cares that these calls throw.
    EXPECT_THROW((void)checked_add(kMax, 1), ResourceExceeded);
    EXPECT_THROW((void)checked_add(kMax, kMax), ResourceExceeded);
}

TEST(CheckedMulTest, MultipliesNormally) {
    EXPECT_EQ(checked_mul(6, 7), 42U);
}

TEST(CheckedMulTest, HandlesZero) {
    EXPECT_EQ(checked_mul(0, kMax), 0U);
    EXPECT_EQ(checked_mul(kMax, 0), 0U);
}

TEST(CheckedMulTest, AllowsResultAtMax) {
    EXPECT_EQ(checked_mul(kMax, 1), kMax);
    EXPECT_EQ(checked_mul(1, kMax), kMax);
}

TEST(CheckedMulTest, ThrowsResourceExceededOnOverflow) {
    EXPECT_THROW((void)checked_mul(kMax, 2), ResourceExceeded);
    // 2^32 * 2^32 = 2^64, one past the representable range.
    EXPECT_THROW((void)checked_mul(std::uint64_t{1} << 32, std::uint64_t{1} << 32),
                 ResourceExceeded);
}

TEST(CheckedPow2Test, ReturnsExpectedValuesAtBoundaries) {
    EXPECT_EQ(checked_pow2(0), 1U);
    EXPECT_EQ(checked_pow2(1), 2U);
    EXPECT_EQ(checked_pow2(62), std::uint64_t{1} << 62);
}

TEST(CheckedPow2Test, ThrowsResourceExceededAtAndAboveTheLimit) {
    EXPECT_THROW((void)checked_pow2(63), ResourceExceeded);
    EXPECT_THROW((void)checked_pow2(64), ResourceExceeded);
}

}  // namespace
}  // namespace menos::common
