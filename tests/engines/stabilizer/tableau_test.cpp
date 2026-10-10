#include "engines/stabilizer/tableau.h"

#include <gtest/gtest.h>

namespace menos::engines::stabilizer {
namespace {

TEST(TableauTest, InitialStateIsDeterministicAllZero) {
    const Tableau tableau(3);
    const Tableau::AffineSubspace subspace = tableau.reduce();
    EXPECT_TRUE(subspace.generators.empty());
    ASSERT_EQ(subspace.base.size(), 3U);
    EXPECT_FALSE(subspace.base[0]);
    EXPECT_FALSE(subspace.base[1]);
    EXPECT_FALSE(subspace.base[2]);
}

TEST(TableauTest, ApplyXFlipsThatQubitsDeterministicOutcome) {
    Tableau tableau(3);
    tableau.apply_x(1);
    const Tableau::AffineSubspace subspace = tableau.reduce();
    EXPECT_TRUE(subspace.generators.empty());
    EXPECT_FALSE(subspace.base[0]);
    EXPECT_TRUE(subspace.base[1]);
    EXPECT_FALSE(subspace.base[2]);
}

TEST(TableauTest, ApplyHMakesThatQubitRandomAndIndependent) {
    Tableau tableau(2);
    tableau.apply_h(0);
    const Tableau::AffineSubspace subspace = tableau.reduce();
    ASSERT_EQ(subspace.generators.size(), 1U);
    EXPECT_TRUE(subspace.generators[0][0]);   // flips qubit 0
    EXPECT_FALSE(subspace.generators[0][1]);  // qubit 1 unaffected
    EXPECT_FALSE(subspace.base[1]);           // qubit 1 stays deterministic 0
}

// Y|0> = i|1> (global phase irrelevant to Z-basis measurement): must
// give the same deterministic outcome as X|0> = |1>. Exercises apply_y's
// sign-flip formula directly (docs/adr/0006).
TEST(TableauTest, ApplyYGivesSameDeterministicOutcomeAsX) {
    Tableau via_y(1);
    via_y.apply_y(0);
    Tableau via_x(1);
    via_x.apply_x(0);
    EXPECT_EQ(via_y.reduce().base, via_x.reduce().base);
    EXPECT_TRUE(via_y.reduce().generators.empty());
}

// H Z H = X exactly: the classic sign-tracking stress case, since it
// requires H's and Z's sign-flip rules to interact correctly across two
// gates rather than being individually trivial (docs/BUILD_PLAN.md
// Phase 4: "sign tracking is the classic bug").
TEST(TableauTest, HZHEqualsXOnDeterministicOutcome) {
    Tableau via_hzh(1);
    via_hzh.apply_h(0);
    via_hzh.apply_z(0);
    via_hzh.apply_h(0);
    Tableau via_x(1);
    via_x.apply_x(0);
    EXPECT_EQ(via_hzh.reduce().base, via_x.reduce().base);
    EXPECT_TRUE(via_hzh.reduce().generators.empty());
}

// S^4 = I exactly: four S applications must return to the same affine
// subspace as before any S was applied, stress-testing that S's
// sign-flip does not accumulate incorrectly.
TEST(TableauTest, FourApplicationsOfSReturnsToOriginalDistribution) {
    Tableau baseline(1);
    baseline.apply_h(0);
    const Tableau::AffineSubspace expected = baseline.reduce();

    Tableau four_s(1);
    four_s.apply_h(0);
    four_s.apply_s(0);
    four_s.apply_s(0);
    four_s.apply_s(0);
    four_s.apply_s(0);
    const Tableau::AffineSubspace actual = four_s.reduce();

    EXPECT_EQ(actual.base, expected.base);
    ASSERT_EQ(actual.generators.size(), expected.generators.size());
    for (std::size_t i = 0; i < actual.generators.size(); ++i) {
        EXPECT_EQ(actual.generators[i], expected.generators[i]);
    }
}

// S then Sdg is the identity.
TEST(TableauTest, SdgUndoesS) {
    Tableau baseline(1);
    baseline.apply_h(0);
    const Tableau::AffineSubspace expected = baseline.reduce();

    Tableau s_then_sdg(1);
    s_then_sdg.apply_h(0);
    s_then_sdg.apply_s(0);
    s_then_sdg.apply_sdg(0);
    const Tableau::AffineSubspace actual = s_then_sdg.reduce();

    EXPECT_EQ(actual.base, expected.base);
}

// SX then SXdg is the identity -- the X-basis-adjacent gate pair,
// exercised the same way as S/Sdg above.
TEST(TableauTest, SXdgUndoesSX) {
    Tableau baseline(1);
    baseline.apply_h(0);
    const Tableau::AffineSubspace expected = baseline.reduce();

    Tableau sx_then_sxdg(1);
    sx_then_sxdg.apply_h(0);
    sx_then_sxdg.apply_sx(0);
    sx_then_sxdg.apply_sxdg(0);
    const Tableau::AffineSubspace actual = sx_then_sxdg.reduce();

    EXPECT_EQ(actual.base, expected.base);
}

// H before Measure (X-basis measurement, the spec's own named
// sign-bug-prone case): H, S, S (=Z), H must equal plain X -- forces
// the H/S interaction to be checked in the specific "measure in the
// X-basis" configuration the spec calls out.
TEST(TableauTest, HSSHEqualsXOnDeterministicOutcome) {
    Tableau via_hssh(1);
    via_hssh.apply_h(0);
    via_hssh.apply_s(0);
    via_hssh.apply_s(0);
    via_hssh.apply_h(0);
    Tableau via_x(1);
    via_x.apply_x(0);
    EXPECT_EQ(via_hssh.reduce().base, via_x.reduce().base);
}

TEST(TableauTest, BellPairCXProducesPerfectlyCorrelatedFreeQubits) {
    Tableau tableau(2);
    tableau.apply_h(0);
    tableau.apply_cx(0, 1);
    const Tableau::AffineSubspace subspace = tableau.reduce();
    ASSERT_EQ(subspace.generators.size(), 1U);
    EXPECT_EQ(subspace.generators[0][0], subspace.generators[0][1]);
    EXPECT_TRUE(subspace.generators[0][0]) << "the single generator must flip both qubits together";
    EXPECT_EQ(subspace.base[0], subspace.base[1]);
}

TEST(TableauTest, SwapExchangesTwoQubitsDeterministicOutcomes) {
    Tableau tableau(2);
    tableau.apply_x(0);
    tableau.apply_swap(0, 1);
    const Tableau::AffineSubspace subspace = tableau.reduce();
    EXPECT_TRUE(subspace.generators.empty());
    EXPECT_FALSE(subspace.base[0]);
    EXPECT_TRUE(subspace.base[1]);
}

TEST(TableauTest, NumQubitsAccessor) {
    const Tableau tableau(7);
    EXPECT_EQ(tableau.num_qubits(), 7U);
}

PauliString make_pauli(std::uint32_t n,
                       const std::vector<std::uint32_t>& x_qubits,
                       const std::vector<std::uint32_t>& z_qubits) {
    PauliString p{n, std::vector<std::uint64_t>(1, 0), std::vector<std::uint64_t>(1, 0)};
    for (std::uint32_t q : x_qubits) {
        p.x_words[0] |= (std::uint64_t{1} << q);
    }
    for (std::uint32_t q : z_qubits) {
        p.z_words[0] |= (std::uint64_t{1} << q);
    }
    return p;
}

TEST(TableauExpectationTest, InitialStateZOnAnyQubitIsPlusOne) {
    const Tableau tableau(2);
    const auto sign = tableau.expectation_sign(make_pauli(2, {}, {0}));  // Z_0
    ASSERT_TRUE(sign.has_value());
    // ASSERT_TRUE above already guarantees this; clang-tidy can't see
    // through the gtest macro.
    EXPECT_FALSE(*sign);  // false = +1  // NOLINT(bugprone-unchecked-optional-access)
}

TEST(TableauExpectationTest, InitialStateXOnAnyQubitAnticommutesGivingZero) {
    const Tableau tableau(2);
    const auto sign = tableau.expectation_sign(make_pauli(2, {0}, {}));  // X_0
    EXPECT_FALSE(sign.has_value());
}

TEST(TableauExpectationTest, PlusStateXIsPlusOneZAnticommutes) {
    Tableau tableau(1);
    tableau.apply_h(0);
    const auto x_sign = tableau.expectation_sign(make_pauli(1, {0}, {}));
    ASSERT_TRUE(x_sign.has_value());
    // ASSERT_TRUE above already guarantees this; clang-tidy can't see
    // through the gtest macro.
    EXPECT_FALSE(*x_sign);  // <+|X|+> = +1  // NOLINT(bugprone-unchecked-optional-access)
    const auto z_sign = tableau.expectation_sign(make_pauli(1, {}, {0}));
    EXPECT_FALSE(z_sign.has_value());  // <+|Z|+> = 0 (anticommutes)
}

// Bell pair: stabilizers X_0X_1 (+1) and Z_0Z_1 (+1); their product
// Y_0Y_1 must be -1 -- hand-derived in docs/adr/0007 via rowsum's own
// phase table (g(X,Z)=3 at each qubit, total phase 6 = 2 mod 4 -> sign
// flip), exercising expectation_sign's reconstructed-product path, not
// just a single matching generator.
TEST(TableauExpectationTest, BellPairStabilizersAndTheirProduct) {
    Tableau tableau(2);
    tableau.apply_h(0);
    tableau.apply_cx(0, 1);

    const auto xx = tableau.expectation_sign(make_pauli(2, {0, 1}, {}));
    ASSERT_TRUE(xx.has_value());
    // ASSERT_TRUE above already guarantees this; clang-tidy can't see
    // through the gtest macro.
    EXPECT_FALSE(*xx);  // X0X1 = +1  // NOLINT(bugprone-unchecked-optional-access)

    const auto zz = tableau.expectation_sign(make_pauli(2, {}, {0, 1}));
    ASSERT_TRUE(zz.has_value());
    EXPECT_FALSE(*zz);  // Z0Z1 = +1  // NOLINT(bugprone-unchecked-optional-access)

    const auto yy = tableau.expectation_sign(make_pauli(2, {0, 1}, {0, 1}));
    ASSERT_TRUE(yy.has_value());
    EXPECT_TRUE(*yy);  // Y0Y1 = -1  // NOLINT(bugprone-unchecked-optional-access)

    const auto z0_alone = tableau.expectation_sign(make_pauli(2, {}, {0}));
    EXPECT_FALSE(z0_alone.has_value());  // anticommutes with X0X1
}

}  // namespace
}  // namespace menos::engines::stabilizer
