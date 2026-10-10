#include "ir/noise.h"

#include <gtest/gtest.h>

#include <complex>
#include <optional>
#include <string>
#include <vector>

namespace menos::ir {
namespace {

// Each optional is guarded by ASSERT_TRUE(...has_value()) before it is read,
// but clang-tidy cannot trace through GoogleTest's early return (the same
// suppression as tests/analysis/cut_test.cpp).
// NOLINTBEGIN(bugprone-unchecked-optional-access)

TEST(NoiseTest, ANoiselessCircuitHasNoReason) {
    Circuit circuit(2, 2);
    circuit.append(Opcode::H, {0});
    circuit.append_unitary({1}, {0.0, 1.0, 1.0, 0.0});
    circuit.append_measure(0, 0);
    circuit.append_measure(1, 1);
    EXPECT_FALSE(first_noise_reason(circuit.operations()).has_value());
}

TEST(NoiseTest, NamesTheFirstChannelByIndex) {
    Circuit circuit(1, 0);
    circuit.append(Opcode::H, {0});
    circuit.append_channel({0}, {{1.0, 0.0, 0.0, 1.0}});
    const std::optional<std::string> reason = first_noise_reason(circuit.operations());
    ASSERT_TRUE(reason.has_value());
    EXPECT_EQ(reason.value(),
              "operation 1 is a noise channel; this engine simulates pure states only");
}

TEST(NoiseTest, NamesAMeasurementWithAReadoutError) {
    Circuit circuit(2, 2);
    circuit.append_measure(0, 0);
    circuit.append_measure(1, 1, ReadoutError{{0.9, 0.1, 0.2, 0.8}});
    const std::optional<std::string> reason = first_noise_reason(circuit.operations());
    ASSERT_TRUE(reason.has_value());
    EXPECT_EQ(reason.value(),
              "operation 1 is a measurement with a readout error; this engine does not model "
              "readout error");
}

// NOLINTEND(bugprone-unchecked-optional-access)

}  // namespace
}  // namespace menos::ir
