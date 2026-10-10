#include "ir/validate.h"

#include <gtest/gtest.h>

#include <limits>

#include "common/error.h"

namespace menos::ir {
namespace {

using common::InvalidCircuit;
using common::UnsupportedFeature;

TEST(ValidateTest, AcceptsAWellFormedCircuit) {
    Circuit circuit(2, 2);
    circuit.append(Opcode::H, {0});
    circuit.append(Opcode::CX, {0, 1});
    circuit.append_measure(0, 0);
    circuit.append_measure(1, 1);
    EXPECT_NO_THROW(validate(circuit));
}

TEST(ValidateTest, ZeroQubitsThrowsInvalidCircuit) {
    const Circuit circuit(0, 0);
    EXPECT_THROW(validate(circuit), InvalidCircuit);
}

TEST(ValidateTest, RepeatedOperandThrowsInvalidCircuit) {
    Circuit circuit(2, 0);
    circuit.append(Opcode::CX, {0, 0});
    EXPECT_THROW(validate(circuit), InvalidCircuit);
}

TEST(ValidateTest, OutOfRangeQubitIndexThrowsInvalidCircuit) {
    Circuit circuit(2, 0);
    circuit.append(Opcode::X, {5});
    EXPECT_THROW(validate(circuit), InvalidCircuit);
}

TEST(ValidateTest, OutOfRangeClbitIndexThrowsInvalidCircuit) {
    Circuit circuit(1, 1);
    circuit.append_measure(0, 5);
    EXPECT_THROW(validate(circuit), InvalidCircuit);
}

TEST(ValidateTest, NanParameterThrowsInvalidCircuit) {
    Circuit circuit(1, 0);
    circuit.append(Opcode::RX, {0}, {std::numeric_limits<double>::quiet_NaN()});
    EXPECT_THROW(validate(circuit), InvalidCircuit);
}

TEST(ValidateTest, WrongParamCountThrowsInvalidCircuit) {
    Circuit circuit(1, 0);
    circuit.append(Opcode::RX, {0}, {});
    EXPECT_THROW(validate(circuit), InvalidCircuit);
}

TEST(ValidateTest, WrongArityThrowsInvalidCircuit) {
    Circuit circuit(2, 0);
    circuit.append(Opcode::X, {0, 1});
    EXPECT_THROW(validate(circuit), InvalidCircuit);
}

TEST(ValidateTest, NonUnitaryMatrixThrowsInvalidCircuit) {
    Circuit circuit(1, 0);
    circuit.append_unitary({0}, {1.0, 1.0, 0.0, 1.0});  // not unitary
    EXPECT_THROW(validate(circuit), InvalidCircuit);
}

TEST(ValidateTest, WrongDimensionUnitaryThrowsInvalidCircuit) {
    Circuit circuit(1, 0);
    circuit.append_unitary({0}, {1.0, 0.0, 0.0});  // 3 entries, needs 4
    EXPECT_THROW(validate(circuit), InvalidCircuit);
}

TEST(ValidateTest, NonFiniteUnitaryEntryThrowsInvalidCircuit) {
    Circuit circuit(1, 0);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    circuit.append_unitary({0}, {std::complex<double>(nan, 0.0), 0.0, 0.0, 1.0});
    EXPECT_THROW(validate(circuit), InvalidCircuit);
}

TEST(ValidateTest, TooManyUnitaryQubitsThrowsUnsupportedFeature) {
    Circuit circuit(6, 0);
    const std::size_t dim = std::size_t{1} << 6;
    std::vector<std::complex<double>> identity(dim * dim, {0.0, 0.0});
    for (std::size_t i = 0; i < dim; ++i) {
        identity[i * dim + i] = {1.0, 0.0};
    }
    circuit.append_unitary({0, 1, 2, 3, 4, 5}, identity);
    EXPECT_THROW(validate(circuit), UnsupportedFeature);
}

TEST(ValidateTest, NonTerminalMeasurementThrowsUnsupportedFeature) {
    Circuit circuit(1, 1);
    circuit.append_measure(0, 0);
    circuit.append(Opcode::X, {0});  // gate after measurement on same qubit
    EXPECT_THROW(validate(circuit), UnsupportedFeature);
}

TEST(ValidateTest, RemeasuringTheSameQubitIsAllowed) {
    Circuit circuit(1, 2);
    circuit.append(Opcode::X, {0});
    circuit.append_measure(0, 0);
    circuit.append_measure(0, 1);  // two measurements, no gate in between: fine
    EXPECT_NO_THROW(validate(circuit));
}

// Phase 3 removes the Phase 1 64-classical-bit ceiling entirely
// (docs/BUILD_PLAN.md 3.8: "no hidden limit of 64 classical bits
// remains"); BitString is now the one production count-key
// representation, arbitrary width. 63/64/65/100 are the widths
// docs/BUILD_PLAN.md 3.8 explicitly names for migration testing.
TEST(ValidateTest, WideClbitCountsAreAllowedWithNoCeiling) {
    for (const std::uint32_t width : {63U, 64U, 65U, 100U, 200U}) {
        const Circuit circuit(1, width);
        EXPECT_NO_THROW(validate(circuit)) << "width=" << width;
    }
}

TEST(ValidateTest, AppendingUnitaryOpcodeThroughAppendThrowsInvalidCircuit) {
    Circuit circuit(1, 0);
    circuit.append(Opcode::Unitary, {0});
    EXPECT_THROW(validate(circuit), InvalidCircuit);
}

TEST(ValidateOutputRequestTest, ShotsWithNoMeasureThrowsInvalidCircuit) {
    Circuit circuit(1, 1);
    circuit.append(Opcode::X, {0});
    EXPECT_THROW(validate_output_request(circuit, 100, false, false), InvalidCircuit);
}

TEST(ValidateOutputRequestTest, ShotsWithMeasureIsAllowed) {
    Circuit circuit(1, 1);
    circuit.append(Opcode::X, {0});
    circuit.append_measure(0, 0);
    EXPECT_NO_THROW(validate_output_request(circuit, 100, false, false));
}

TEST(ValidateOutputRequestTest, NothingRequestedThrowsInvalidCircuit) {
    const Circuit circuit(1, 0);
    EXPECT_THROW(validate_output_request(circuit, 0, false, false), InvalidCircuit);
}

TEST(ValidateOutputRequestTest, ZeroShotsWithStatevectorIsAllowed) {
    const Circuit circuit(1, 0);
    EXPECT_NO_THROW(validate_output_request(circuit, 0, true, false));
}

TEST(ValidateOutputRequestTest, ZeroShotsWithExpectationOnlyIsAllowed) {
    const Circuit circuit(1, 0);
    EXPECT_NO_THROW(validate_output_request(circuit, 0, false, /*want_expectation=*/true));
}

}  // namespace
}  // namespace menos::ir
