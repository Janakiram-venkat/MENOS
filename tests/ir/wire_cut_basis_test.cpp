// docs/adr/0013 section 2.1: the wire-cut decomposition, tested on its own,
// with no engine, runtime or planner involved. If these fail, nothing built
// on top of them can be trusted, and the failure is in the mathematics rather
// than in the machinery that uses it.

#include "ir/wire_cut_basis.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <complex>
#include <map>
#include <vector>

#include "ir/gate_table.h"

namespace menos::ir {
namespace {

using Complex = std::complex<double>;
constexpr double kTolerance = 1e-12;

// The four Pauli matrices, row-major, written out rather than pulled from the
// gate table, so this test does not validate the decomposition against the
// same source the decomposition itself uses.
std::array<Complex, 4> pauli_matrix(CutObservable observable) {
    switch (observable) {
        case CutObservable::I:
            return {Complex{1, 0}, Complex{0, 0}, Complex{0, 0}, Complex{1, 0}};
        case CutObservable::X:
            return {Complex{0, 0}, Complex{1, 0}, Complex{1, 0}, Complex{0, 0}};
        case CutObservable::Y:
            return {Complex{0, 0}, Complex{0, -1}, Complex{0, 1}, Complex{0, 0}};
        case CutObservable::Z:
            return {Complex{1, 0}, Complex{0, 0}, Complex{0, 0}, Complex{-1, 0}};
    }
    ADD_FAILURE() << "unknown observable";
    return {};
}

// The state vector each preparation is supposed to produce, written out
// independently of preparation_gates() so the two can be compared.
std::array<Complex, 2> expected_state(CutPrepState state) {
    const double r = 1.0 / std::sqrt(2.0);
    switch (state) {
        case CutPrepState::Zero:
            return {Complex{1, 0}, Complex{0, 0}};
        case CutPrepState::One:
            return {Complex{0, 0}, Complex{1, 0}};
        case CutPrepState::Plus:
            return {Complex{r, 0}, Complex{r, 0}};
        case CutPrepState::Minus:
            return {Complex{r, 0}, Complex{-r, 0}};
        case CutPrepState::PlusI:
            return {Complex{r, 0}, Complex{0, r}};
        case CutPrepState::MinusI:
            return {Complex{r, 0}, Complex{0, -r}};
    }
    ADD_FAILURE() << "unknown state";
    return {};
}

// Applies a single-qubit gate matrix to a 2-vector.
std::array<Complex, 2> apply(const std::array<Complex, 4>& m, const std::array<Complex, 2>& v) {
    return {m[0] * v[0] + m[1] * v[1], m[2] * v[0] + m[3] * v[1]};
}

TEST(WireCutBasisTest, ThereAreExactlyEightTerms) {
    EXPECT_EQ(wire_cut_terms().size(), kWireCutTermCount);
    EXPECT_EQ(kWireCutTermCount, 8U);
}

// Every coefficient is exactly +-1/2. The 1/2 is 1/dim and is load-bearing:
// dropping it scales every reconstructed value by 2^K.
TEST(WireCutBasisTest, EveryCoefficientIsExactlyHalfInMagnitude) {
    for (const WireCutTerm& term : wire_cut_terms()) {
        EXPECT_DOUBLE_EQ(std::abs(term.coefficient), 0.5);
    }
}

// The exact table from docs/adr/0013 section 2.1, order included. This is the
// regression guard: it fails if any sign, pairing or ordering is altered.
TEST(WireCutBasisTest, TableMatchesTheCitedDecompositionExactly) {
    const std::array<WireCutTerm, 8> expected = {{
        {CutObservable::I, CutPrepState::Zero, 0.5},
        {CutObservable::I, CutPrepState::One, 0.5},
        {CutObservable::X, CutPrepState::Plus, 0.5},
        {CutObservable::X, CutPrepState::Minus, -0.5},
        {CutObservable::Y, CutPrepState::PlusI, 0.5},
        {CutObservable::Y, CutPrepState::MinusI, -0.5},
        {CutObservable::Z, CutPrepState::Zero, 0.5},
        {CutObservable::Z, CutPrepState::One, -0.5},
    }};
    const std::span<const WireCutTerm> actual = wire_cut_terms();
    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(actual[i].observable, expected[i].observable) << "term " << i;
        EXPECT_EQ(actual[i].prepared, expected[i].prepared) << "term " << i;
        EXPECT_DOUBLE_EQ(actual[i].coefficient, expected[i].coefficient) << "term " << i;
    }
}

// The cross-check docs/adr/0013 insists on showing rather than asserting: the
// 1-norm of the decomposition is 4, which is independently the optimal
// gamma-factor for a wire cut without classical communication (Brenner,
// Piveteau & Sutter, arXiv:2302.03366, Proposition 4.1). Two independent
// sources, one constant.
TEST(WireCutBasisTest, OneNormIsFourMatchingTheIndependentlyProvenGammaFactor) {
    double one_norm = 0.0;
    for (const WireCutTerm& term : wire_cut_terms()) {
        one_norm += std::abs(term.coefficient);
    }
    EXPECT_DOUBLE_EQ(one_norm, 4.0);
}

// THE identity, checked numerically: for an arbitrary single-qubit density
// matrix rho, sum_i c_i * tr(O_i rho) * rho_i must reproduce rho exactly.
// This is the claim everything else rests on.
TEST(WireCutBasisTest, DecompositionReproducesAnArbitraryDensityMatrix) {
    // A deliberately asymmetric, non-diagonal, physical rho:
    // rho = 0.7|psi><psi| + 0.3|phi><phi| with non-trivial phases.
    const std::array<Complex, 2> psi = {Complex{0.6, 0.0}, Complex{0.48, 0.64}};
    const std::array<Complex, 2> phi = {Complex{0.8, 0.0}, Complex{-0.36, 0.48}};
    std::array<Complex, 4> rho{};
    for (std::size_t r = 0; r < 2; ++r) {
        for (std::size_t c = 0; c < 2; ++c) {
            rho[r * 2 + c] = 0.7 * psi[r] * std::conj(psi[c]) + 0.3 * phi[r] * std::conj(phi[c]);
        }
    }

    std::array<Complex, 4> reconstructed{};
    for (const WireCutTerm& term : wire_cut_terms()) {
        // tr(O_i . rho)
        const std::array<Complex, 4> o = pauli_matrix(term.observable);
        const Complex trace = o[0] * rho[0] + o[1] * rho[2] + o[2] * rho[1] + o[3] * rho[3];

        // rho_i = |state><state|
        const std::array<Complex, 2> s = expected_state(term.prepared);
        for (std::size_t r = 0; r < 2; ++r) {
            for (std::size_t c = 0; c < 2; ++c) {
                reconstructed[r * 2 + c] += term.coefficient * trace * s[r] * std::conj(s[c]);
            }
        }
    }

    for (std::size_t i = 0; i < 4; ++i) {
        EXPECT_NEAR(reconstructed[i].real(), rho[i].real(), kTolerance) << "entry " << i;
        EXPECT_NEAR(reconstructed[i].imag(), rho[i].imag(), kTolerance) << "entry " << i;
    }
}

// preparation_gates() really does produce the eigenstate it claims, checked
// by applying the gate-table matrices to |0>.
TEST(WireCutBasisTest, PreparationGatesProduceTheClaimedEigenstates) {
    for (const CutPrepState state : {CutPrepState::Zero,
                                     CutPrepState::One,
                                     CutPrepState::Plus,
                                     CutPrepState::Minus,
                                     CutPrepState::PlusI,
                                     CutPrepState::MinusI}) {
        std::array<Complex, 2> v = {Complex{1, 0}, Complex{0, 0}};
        for (const Opcode op : preparation_gates(state)) {
            const std::vector<Complex> m = generate_matrix(op, {});
            ASSERT_EQ(m.size(), 4U);
            v = apply({m[0], m[1], m[2], m[3]}, v);
        }
        const std::array<Complex, 2> want = expected_state(state);
        EXPECT_NEAR(v[0].real(), want[0].real(), kTolerance);
        EXPECT_NEAR(v[0].imag(), want[0].imag(), kTolerance);
        EXPECT_NEAR(v[1].real(), want[1].real(), kTolerance);
        EXPECT_NEAR(v[1].imag(), want[1].imag(), kTolerance);
    }
}

// Every preparation gate must be Clifford, or a stabilizer fragment would
// stop being stabilizer-eligible purely because a cut was placed on it.
TEST(WireCutBasisTest, EveryPreparationGateIsClifford) {
    for (const CutPrepState state : {CutPrepState::Zero,
                                     CutPrepState::One,
                                     CutPrepState::Plus,
                                     CutPrepState::Minus,
                                     CutPrepState::PlusI,
                                     CutPrepState::MinusI}) {
        for (const Opcode op : preparation_gates(state)) {
            EXPECT_TRUE(op == Opcode::X || op == Opcode::H || op == Opcode::S)
                << "non-Clifford preparation gate for state " << static_cast<int>(state);
        }
    }
}

TEST(WireCutBasisTest, DistinctOperandCountsMatchTheTable) {
    std::map<CutObservable, int> observables;
    std::map<CutPrepState, int> states;
    for (const WireCutTerm& term : wire_cut_terms()) {
        observables[term.observable] += 1;
        states[term.prepared] += 1;
    }
    // These two numbers drive the cost model's distinct-evaluation count
    // (docs/adr/0013 Eq. 4.1); if they drift, the cost model silently lies.
    EXPECT_EQ(observables.size(), kDistinctCutObservables);
    EXPECT_EQ(states.size(), kDistinctCutPrepStates);
}

TEST(WireCutBasisTest, ObservableBitsFollowThePauliStringConvention) {
    EXPECT_FALSE(observable_bits(CutObservable::I).x);
    EXPECT_FALSE(observable_bits(CutObservable::I).z);
    EXPECT_TRUE(observable_bits(CutObservable::X).x);
    EXPECT_FALSE(observable_bits(CutObservable::X).z);
    EXPECT_FALSE(observable_bits(CutObservable::Z).x);
    EXPECT_TRUE(observable_bits(CutObservable::Z).z);
    // Y = i*X*Z, so both bits set (include/menos/observable.h's convention).
    EXPECT_TRUE(observable_bits(CutObservable::Y).x);
    EXPECT_TRUE(observable_bits(CutObservable::Y).z);
}

}  // namespace
}  // namespace menos::ir
