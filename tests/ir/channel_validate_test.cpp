// Validation of noise channels and readout errors (Phase 12, docs/adr/0016
// sections 3 and 9): trace preservation and readout rows within
// common::kDefaultAbsTol, matrix shapes, finite entries, operand count, and
// the terminal-measurement rule.

#include <gtest/gtest.h>

#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <vector>

#include "common/error.h"
#include "ir/validate.h"

namespace menos::ir {
namespace {

using common::InvalidCircuit;
using common::UnsupportedFeature;
using Complex = std::complex<double>;
using Kraus = std::vector<std::vector<Complex>>;

// rho -> (1 - p) rho + p I/2, as Kraus operators sqrt(1 - 3p/4) I and
// sqrt(p/4) X, Y, Z.
Kraus depolarizing_1q(double p) {
    const double a = std::sqrt(1.0 - 3.0 * p / 4.0);
    const double b = std::sqrt(p / 4.0);
    return {{a, 0.0, 0.0, a},
            {0.0, b, b, 0.0},
            {0.0, Complex{0.0, -b}, Complex{0.0, b}, 0.0},
            {b, 0.0, 0.0, -b}};
}

Kraus amplitude_damping(double gamma) {
    return {{1.0, 0.0, 0.0, std::sqrt(1.0 - gamma)}, {0.0, std::sqrt(gamma), 0.0, 0.0}};
}

// A two-qubit identity channel (one Kraus operator, I on 4 dimensions).
Kraus identity_2q() {
    std::vector<Complex> id(16, 0.0);
    for (std::size_t i = 0; i < 4; ++i) {
        id[i * 4 + i] = 1.0;
    }
    return {id};
}

TEST(ChannelValidateTest, AcceptsDepolarizingAndAmplitudeDamping) {
    Circuit circuit(2, 0);
    circuit.append(Opcode::H, {0});
    circuit.append_channel({0}, depolarizing_1q(0.1));
    circuit.append_channel({1}, amplitude_damping(0.3));
    circuit.append_channel({0, 1}, identity_2q());
    EXPECT_NO_THROW(validate(circuit));
}

TEST(ChannelValidateTest, AcceptsTheEdgesOfTheParameterRange) {
    Circuit circuit(1, 0);
    circuit.append_channel({0}, depolarizing_1q(0.0));
    circuit.append_channel({0}, depolarizing_1q(4.0 / 3.0));
    circuit.append_channel({0}, amplitude_damping(1.0));
    EXPECT_NO_THROW(validate(circuit));
}

TEST(ChannelValidateTest, RejectsAChannelThatIsNotTracePreserving) {
    Circuit circuit(1, 0);
    // Amplitude damping with its K1 dropped loses probability from |1>.
    Kraus lossy = amplitude_damping(0.3);
    lossy.pop_back();
    circuit.append_channel({0}, lossy);
    EXPECT_THROW(validate(circuit), InvalidCircuit);
}

TEST(ChannelValidateTest, RejectsADeviationJustAboveTheTolerance) {
    Circuit circuit(1, 0);
    Kraus scaled = depolarizing_1q(0.1);
    // Scaling every operator by (1 + eps) scales sum K^dagger K by (1 + eps)^2:
    // a diagonal deviation of about 2 * eps = 2e-11, above 1e-12.
    for (std::vector<Complex>& k : scaled) {
        for (Complex& entry : k) {
            entry *= 1.0 + 1e-11;
        }
    }
    circuit.append_channel({0}, scaled);
    EXPECT_THROW(validate(circuit), InvalidCircuit);
}

TEST(ChannelValidateTest, AcceptsADeviationJustBelowTheTolerance) {
    Circuit circuit(1, 0);
    Kraus scaled = depolarizing_1q(0.1);
    for (std::vector<Complex>& k : scaled) {
        for (Complex& entry : k) {
            entry *= 1.0 + 1e-14;
        }
    }
    circuit.append_channel({0}, scaled);
    EXPECT_NO_THROW(validate(circuit));
}

TEST(ChannelValidateTest, RejectsAnEmptyKrausList) {
    Circuit circuit(1, 0);
    circuit.append_channel({0}, {});
    EXPECT_THROW(validate(circuit), InvalidCircuit);
}

TEST(ChannelValidateTest, RejectsAWronglySizedKrausOperator) {
    Circuit circuit(2, 0);
    circuit.append_channel({0, 1}, depolarizing_1q(0.1));  // 2x2 operators on 2 qubits
    EXPECT_THROW(validate(circuit), InvalidCircuit);
}

TEST(ChannelValidateTest, RejectsANonFiniteEntry) {
    Circuit circuit(1, 0);
    Kraus bad = depolarizing_1q(0.1);
    bad[1][1] = std::numeric_limits<double>::quiet_NaN();
    circuit.append_channel({0}, bad);
    EXPECT_THROW(validate(circuit), InvalidCircuit);
}

TEST(ChannelValidateTest, RejectsZeroOperands) {
    Circuit circuit(1, 0);
    circuit.append_channel({}, {{1.0}});
    EXPECT_THROW(validate(circuit), InvalidCircuit);
}

TEST(ChannelValidateTest, RejectsMoreThanTheMaximumOperandCount) {
    Circuit circuit(static_cast<std::uint32_t>(kMaxUnitaryQubits) + 1, 0);
    std::vector<QubitId> qubits;
    for (QubitId q = 0; q <= static_cast<QubitId>(kMaxUnitaryQubits); ++q) {
        qubits.push_back(q);
    }
    circuit.append_channel(qubits, {{1.0}});
    EXPECT_THROW(validate(circuit), UnsupportedFeature);
}

TEST(ChannelValidateTest, RejectsAnOutOfRangeOrRepeatedOperand) {
    Circuit out_of_range(1, 0);
    out_of_range.append_channel({3}, depolarizing_1q(0.1));
    EXPECT_THROW(validate(out_of_range), InvalidCircuit);

    Circuit repeated(2, 0);
    repeated.append_channel({1, 1}, identity_2q());
    EXPECT_THROW(validate(repeated), InvalidCircuit);
}

TEST(ChannelValidateTest, RejectsAChannelAfterATerminalMeasurement) {
    Circuit circuit(1, 1);
    circuit.append_measure(0, 0);
    circuit.append_channel({0}, depolarizing_1q(0.1));
    EXPECT_THROW(validate(circuit), UnsupportedFeature);
}

TEST(ReadoutValidateTest, AcceptsAValidAssignmentMatrix) {
    Circuit circuit(1, 1);
    circuit.append_measure(0, 0, ReadoutError{{0.97, 0.03, 0.05, 0.95}});
    EXPECT_NO_THROW(validate(circuit));
}

TEST(ReadoutValidateTest, RejectsAProbabilityOutsideTheUnitInterval) {
    Circuit circuit(1, 1);
    circuit.append_measure(0, 0, ReadoutError{{1.1, -0.1, 0.0, 1.0}});
    EXPECT_THROW(validate(circuit), InvalidCircuit);
}

TEST(ReadoutValidateTest, RejectsARowThatDoesNotSumToOne) {
    Circuit circuit(1, 1);
    circuit.append_measure(0, 0, ReadoutError{{0.9, 0.05, 0.05, 0.95}});
    EXPECT_THROW(validate(circuit), InvalidCircuit);
}

TEST(ReadoutValidateTest, RejectsANonFiniteProbability) {
    Circuit circuit(1, 1);
    circuit.append_measure(
        0, 0, ReadoutError{{std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0, 1.0}});
    EXPECT_THROW(validate(circuit), InvalidCircuit);
}

}  // namespace
}  // namespace menos::ir
