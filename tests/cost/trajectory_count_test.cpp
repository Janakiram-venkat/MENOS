#include "cost/trajectory_count.h"

#include <gtest/gtest.h>

#include <cmath>
#include <complex>
#include <cstdint>

#include "common/error.h"

namespace menos::cost {
namespace {

using Complex = std::complex<double>;

Observable single_term(std::uint32_t n, std::uint64_t x, std::uint64_t z, Complex c = 1.0) {
    Observable observable;
    observable.terms.emplace_back(PauliString{n, {x}, {z}}, c);
    return observable;
}

TEST(TrajectoryCountTest, CountsOnlyUsesOneTrajectoryPerShot) {
    const TrajectoryCount count = trajectory_count(1000, nullptr, std::nullopt);
    EXPECT_EQ(count.trajectories, 1000U);
    EXPECT_FALSE(count.coefficient_bound.has_value());
}

TEST(TrajectoryCountTest, ExpectationCountIsCeilOfBoundOverPrecisionSquared) {
    Observable observable = single_term(2, 1, 0, Complex{0.5, 0.0});
    observable.terms.emplace_back(PauliString{2, {0}, {2}}, Complex{0.0, -0.25});
    observable.terms.emplace_back(PauliString{2, {0}, {0}}, Complex{3.0, 0.0});  // identity
    // C = 0.5 + 0.25 = 0.75 (the identity term is excluded); C^2 / eps^2 at
    // eps = 0.01 is 5625.
    const TrajectoryCount count = trajectory_count(0, &observable, 0.01);
    ASSERT_TRUE(count.coefficient_bound.has_value());
    // Guarded by the ASSERT_TRUE above; clang-tidy cannot see through it.
    const double c = *count.coefficient_bound;  // NOLINT(bugprone-unchecked-optional-access)
    EXPECT_DOUBLE_EQ(c, 0.75);
    EXPECT_EQ(count.trajectories, 5625U);
    EXPECT_LE(c / std::sqrt(static_cast<double>(count.trajectories)), 0.01);
    // Shots dominate when larger, and N is never below 2.
    EXPECT_EQ(trajectory_count(9000, &observable, 0.01).trajectories, 9000U);
    const Observable identity_only = single_term(1, 0, 0, Complex{2.0, 0.0});
    EXPECT_EQ(trajectory_count(0, &identity_only, 0.5).trajectories, 2U);
}

TEST(TrajectoryCountTest, AnUnrepresentableCountIsRefused) {
    const Observable observable = single_term(1, 1, 0);
    EXPECT_THROW((void)trajectory_count(0, &observable, 1e-200), common::ResourceExceeded);
}

}  // namespace
}  // namespace menos::cost
