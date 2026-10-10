#include "menos/menos.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

#include "common/tolerance.h"

namespace menos {
namespace {

using Complex = std::complex<double>;

void expect_amplitude(const std::vector<Complex>& sv, std::size_t index, Complex expected) {
    ASSERT_LT(index, sv.size());
    EXPECT_NEAR(sv[index].real(), expected.real(), common::kDefaultAbsTol) << "index " << index;
    EXPECT_NEAR(sv[index].imag(), expected.imag(), common::kDefaultAbsTol) << "index " << index;
}

// Every test in this file passes want_statevector = true with shots = 0,
// so a statevector is always present (menos::ir::validate_output_request
// requires it when shots == 0). Centralizing the access here means
// clang-tidy's unchecked-optional-access NOLINT lives in one place.
const std::vector<Complex>& get_statevector(const Result& result) {
    EXPECT_TRUE(result.statevector().has_value());
    return *result.statevector();  // NOLINT(bugprone-unchecked-optional-access)
}

TEST(AnalyticCircuitsTest, BellState) {
    Circuit circuit(2, 0);
    circuit.append(Opcode::H, {0});
    circuit.append(Opcode::CX, {0, 1});

    const Result result = run(circuit, RunOptions{.shots = 0, .want_statevector = true, .seed = 0});
    const auto& sv = get_statevector(result);
    ASSERT_EQ(sv.size(), 4U);

    const double s = 1.0 / std::numbers::sqrt2;
    expect_amplitude(sv, 0, {s, 0.0});
    expect_amplitude(sv, 1, {0.0, 0.0});
    expect_amplitude(sv, 2, {0.0, 0.0});
    expect_amplitude(sv, 3, {s, 0.0});
}

TEST(AnalyticCircuitsTest, GhzState) {
    for (std::uint32_t n : {2U, 3U, 5U}) {
        Circuit circuit(n, 0);
        circuit.append(Opcode::H, {0});
        for (QubitId q = 1; q < n; ++q) {
            circuit.append(Opcode::CX, {q - 1, q});
        }
        const Result result =
            run(circuit, RunOptions{.shots = 0, .want_statevector = true, .seed = 0});
        const auto& sv = get_statevector(result);
        const std::size_t dim = std::size_t{1} << n;
        ASSERT_EQ(sv.size(), dim);

        const double s = 1.0 / std::numbers::sqrt2;
        for (std::size_t i = 0; i < dim; ++i) {
            if (i == 0 || i == dim - 1) {
                expect_amplitude(sv, i, {s, 0.0});
            } else {
                expect_amplitude(sv, i, {0.0, 0.0});
            }
        }
    }
}

TEST(AnalyticCircuitsTest, UniformSuperposition) {
    constexpr std::uint32_t n = 4;
    Circuit circuit(n, 0);
    for (QubitId q = 0; q < n; ++q) {
        circuit.append(Opcode::H, {q});
    }
    const Result result = run(circuit, RunOptions{.shots = 0, .want_statevector = true, .seed = 0});
    const auto& sv = get_statevector(result);
    const double expected = 1.0 / std::sqrt(static_cast<double>(sv.size()));
    for (std::size_t i = 0; i < sv.size(); ++i) {
        expect_amplitude(sv, i, {expected, 0.0});
    }
}

TEST(AnalyticCircuitsTest, XBasisStates) {
    // Flipping a subset of qubits with X produces the corresponding basis
    // state deterministically.
    constexpr std::uint32_t n = 4;
    for (std::size_t target_index : {std::size_t{0},
                                     std::size_t{1},
                                     std::size_t{5},
                                     std::size_t{0b1010},
                                     std::size_t{0b1111}}) {
        Circuit circuit(n, 0);
        for (QubitId q = 0; q < n; ++q) {
            if ((target_index >> q) & 1U) {
                circuit.append(Opcode::X, {q});
            }
        }
        const Result result =
            run(circuit, RunOptions{.shots = 0, .want_statevector = true, .seed = 0});
        const auto& sv = get_statevector(result);
        for (std::size_t i = 0; i < sv.size(); ++i) {
            expect_amplitude(sv, i, (i == target_index) ? Complex{1.0, 0.0} : Complex{0.0, 0.0});
        }
    }
}

TEST(AnalyticCircuitsTest, CircuitWhoseOnlyEffectIsAGlobalPhase) {
    // RZ(2*pi) = diag(e^{-i*pi}, e^{i*pi}) = -I: a pure global phase flip,
    // with no effect on measurement probabilities (CLAUDE.md: "RZ is not
    // P. RZ(t) and P(t) differ by a global phase.").
    Circuit circuit(1, 0);
    circuit.append(Opcode::RZ, {0}, {2.0 * std::numbers::pi});
    const Result result = run(circuit, RunOptions{.shots = 0, .want_statevector = true, .seed = 0});
    const auto& sv = get_statevector(result);
    ASSERT_EQ(sv.size(), 2U);
    expect_amplitude(sv, 0, {-1.0, 0.0});
    expect_amplitude(sv, 1, {0.0, 0.0});
}

// A small QFT on a basis input, checked against the closed-form DFT:
// QFT|j> = (1/sqrt(N)) * sum_k exp(2*pi*i*j*k/N) |k>, N = 2^n. j and k use
// the same little-endian bit convention as amplitude indices throughout
// this codebase.
//
// The textbook circuit (H(i), then a controlled phase from every later
// qubit onto i, for i ascending) is usually presented for a big-endian
// qubit numbering and needs a trailing bit-reversal SWAP network to match
// the DFT directly. This codebase is little-endian (qubit 0 = bit 0,
// CLAUDE.md's domain invariant), which mirrors the correction: the
// bit-reversal SWAPs go *before* the H/controlled-phase sequence, not
// after. Verified independently in Python (statevector simulation against
// the closed-form DFT for n = 1..5, every input) before writing this: see
// the commit message for the derivation.
// n (a qubit count, always <= 3 in this file) and input_basis_state (a
// basis-index up to 2^n-1) are not realistically confusable at any call
// site here.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
Circuit build_qft_circuit(std::uint32_t n, std::size_t input_basis_state) {
    Circuit circuit(n, 0);
    for (QubitId q = 0; q < n; ++q) {
        if ((input_basis_state >> q) & 1U) {
            circuit.append(Opcode::X, {q});
        }
    }
    for (QubitId i = 0; i < n / 2; ++i) {
        circuit.append(Opcode::SWAP, {i, n - 1 - i});
    }
    for (QubitId i = 0; i < n; ++i) {
        circuit.append(Opcode::H, {i});
        for (QubitId j = i + 1; j < n; ++j) {
            const double theta = std::numbers::pi / static_cast<double>(std::size_t{1} << (j - i));
            circuit.append(Opcode::CP, {j, i}, {theta});
        }
    }
    return circuit;
}

TEST(AnalyticCircuitsTest, SmallQftOnBasisInputs) {
    constexpr std::uint32_t n = 3;
    const std::size_t dim = std::size_t{1} << n;
    for (std::size_t j = 0; j < dim; ++j) {
        const Circuit circuit = build_qft_circuit(n, j);
        const Result result =
            run(circuit, RunOptions{.shots = 0, .want_statevector = true, .seed = 0});
        const auto& sv = get_statevector(result);
        ASSERT_EQ(sv.size(), dim);

        const double norm = 1.0 / std::sqrt(static_cast<double>(dim));
        for (std::size_t k = 0; k < dim; ++k) {
            const double angle =
                2.0 * std::numbers::pi * static_cast<double>(j * k) / static_cast<double>(dim);
            expect_amplitude(sv, k, Complex{norm * std::cos(angle), norm * std::sin(angle)});
        }
    }
}

}  // namespace
}  // namespace menos
