#include "engines/dense/state_vector.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

#include "common/error.h"

namespace menos::engines::dense {
namespace {

TEST(StateVectorTest, SizedCorrectly) {
    const StateVector state(3, /*budget_bytes=*/1'000'000);
    EXPECT_EQ(state.num_qubits(), 3);
    EXPECT_EQ(state.size_bytes(), 8U * sizeof(std::complex<double>));
    EXPECT_EQ(state.amplitudes().size(), 8U);
}

TEST(StateVectorTest, DataIsAligned) {
    const StateVector state(5, /*budget_bytes=*/1'000'000);
    const auto* ptr = state.amplitudes().data();
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(ptr) % kStateVectorAlignment, 0U);
}

TEST(StateVectorTest, ThrowsResourceExceededBeforeAllocatingWhenBudgetTooSmall) {
    EXPECT_THROW(StateVector(40, /*budget_bytes=*/1024), common::ResourceExceeded);
}

TEST(StateVectorTest, ZeroQubitsIsOneAmplitude) {
    const StateVector state(0, /*budget_bytes=*/1024);
    EXPECT_EQ(state.amplitudes().size(), 1U);
}

TEST(InitializeTest, DefaultsToBasisZeroWithNoPhase) {
    StateVector state(2, /*budget_bytes=*/1'000'000);
    initialize(state, 0.0);
    const auto amps = state.amplitudes();
    EXPECT_EQ(amps[0], std::complex<double>(1.0, 0.0));
    for (std::size_t i = 1; i < amps.size(); ++i) {
        EXPECT_EQ(amps[i], std::complex<double>(0.0, 0.0));
    }
}

TEST(InitializeTest, AppliesGlobalPhase) {
    StateVector state(1, /*budget_bytes=*/1'000'000);
    const double phase = std::numbers::pi / 3.0;
    initialize(state, phase);
    const auto amps = state.amplitudes();
    EXPECT_NEAR(amps[0].real(), std::cos(phase), 1e-12);
    EXPECT_NEAR(amps[0].imag(), std::sin(phase), 1e-12);
    EXPECT_EQ(amps[1], std::complex<double>(0.0, 0.0));
}

}  // namespace
}  // namespace menos::engines::dense
