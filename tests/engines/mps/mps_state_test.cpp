#include "engines/mps/mps_state.h"

#include <gtest/gtest.h>

#include <cmath>
#include <complex>
#include <vector>

#include "mps_test_support.h"

namespace menos::engines::mps {
namespace {

using Complex = std::complex<double>;
constexpr double kTol = 1e-12;

void expect_amplitudes_near(const std::vector<Complex>& actual,
                            const std::vector<Complex>& expected) {
    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t i = 0; i < actual.size(); ++i) {
        EXPECT_NEAR(actual[i].real(), expected[i].real(), kTol) << "index " << i;
        EXPECT_NEAR(actual[i].imag(), expected[i].imag(), kTol) << "index " << i;
    }
}

TEST(MPSStateTest, InitialStateIsAllZeroForVariousQubitCounts) {
    for (const std::uint32_t n : {std::uint32_t{1}, std::uint32_t{2}, std::uint32_t{5}}) {
        MPSState state(n);
        EXPECT_EQ(state.orthogonality_center(), 0u);
        EXPECT_EQ(state.max_bond_dimension(), 1u);
        std::vector<Complex> expected(std::size_t{1} << n, Complex{0.0, 0.0});
        expected[0] = Complex{1.0, 0.0};
        expect_amplitudes_near(reconstruct_statevector(state), expected);
    }
}

// Hand-built Bell state (|00> + |11>) / sqrt(2), center at site 0
// (there is no public setter for MPSState's own orthogonality-center
// bookkeeping, so a hand-built state must match the constructor's
// default of 0 exactly, not an arbitrarily chosen convention): site 0
// carries the two singular values 1/sqrt(2) on its diagonal, site 1 is
// right-isometric (identity map bond->physical) -- the standard
// Schmidt decomposition of the Bell state, not derived from
// move_orthogonality_center_to itself, so it is an independent check
// of that method's correctness below.
MPSState make_bell_state() {
    MPSState state(2);
    const double inv_sqrt2 = 1.0 / std::sqrt(2.0);

    SiteTensor site0{1, 2, std::vector<Complex>(std::size_t{4}, Complex{0.0, 0.0})};
    site0.at(0, 0, 0) = Complex{inv_sqrt2, 0.0};
    site0.at(0, 1, 1) = Complex{inv_sqrt2, 0.0};
    state.site(0) = site0;

    SiteTensor site1{2, 1, std::vector<Complex>(std::size_t{4}, Complex{0.0, 0.0})};
    site1.at(0, 0, 0) = Complex{1.0, 0.0};
    site1.at(1, 1, 0) = Complex{1.0, 0.0};
    state.site(1) = site1;

    return state;
}

TEST(MPSStateTest, HandBuiltBellStateReconstructsCorrectly) {
    const MPSState state = make_bell_state();
    const double inv_sqrt2 = 1.0 / std::sqrt(2.0);
    const std::vector<Complex> expected = {
        Complex{inv_sqrt2, 0.0}, Complex{0.0, 0.0}, Complex{0.0, 0.0}, Complex{inv_sqrt2, 0.0}};
    expect_amplitudes_near(reconstruct_statevector(state), expected);
    EXPECT_EQ(state.bond_dimension(0), 2u);
}

TEST(MPSStateTest, MovingCenterRightAndBackPreservesReconstructedState) {
    MPSState state = make_bell_state();
    ASSERT_EQ(state.orthogonality_center(), 0u);
    const std::vector<Complex> before = reconstruct_statevector(state);

    state.move_orthogonality_center_to(1);
    EXPECT_EQ(state.orthogonality_center(), 1u);
    EXPECT_EQ(state.bond_dimension(0), 2u);
    expect_amplitudes_near(reconstruct_statevector(state), before);

    state.move_orthogonality_center_to(0);
    EXPECT_EQ(state.orthogonality_center(), 0u);
    EXPECT_EQ(state.bond_dimension(0), 2u);
    expect_amplitudes_near(reconstruct_statevector(state), before);
}

TEST(MPSStateTest, MovingCenterToCurrentPositionIsANoOp) {
    MPSState state = make_bell_state();
    const std::vector<Complex> before = reconstruct_statevector(state);
    state.move_orthogonality_center_to(0);
    expect_amplitudes_near(reconstruct_statevector(state), before);
}

TEST(MPSStateTest, MovingCenterAcrossThreeSitesPreservesReconstructedState) {
    // A 3-site product-of-Bell-plus-trivial state built by hand-crafting
    // site 0/1 as the Bell state above and leaving site 2 trivial (|0>)
    // -- exercises move_orthogonality_center_to walking across more than
    // one bond (2 -> 0 and back), not just a single step.
    MPSState state(3);
    const MPSState bell = make_bell_state();
    state.site(0) = bell.site(0);
    state.site(1) = bell.site(1);
    // Reconcile the bond between site 1 and the still-trivial site 2:
    // site 1's chi_right is 1 already (Bell's second site), matching
    // site 2's default chi_left of 1, so no adjustment needed.

    const std::vector<Complex> before = reconstruct_statevector(state);
    state.move_orthogonality_center_to(2);
    expect_amplitudes_near(reconstruct_statevector(state), before);
    state.move_orthogonality_center_to(0);
    expect_amplitudes_near(reconstruct_statevector(state), before);
}

}  // namespace
}  // namespace menos::engines::mps
