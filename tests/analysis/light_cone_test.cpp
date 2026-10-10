#include "analysis/light_cone.h"

#include <gtest/gtest.h>

#include <array>
#include <complex>
#include <variant>
#include <vector>

namespace menos::analysis {
namespace {

TEST(LightConeTest, ChainIsFullyPulledInByItsLastQubit) {
    Circuit circuit(3, 0);
    circuit.append(Opcode::H, {0});
    circuit.append(Opcode::CX, {0, 1});
    circuit.append(Opcode::CX, {1, 2});

    const LightConeResult result = compute_light_cone(circuit, {2});
    EXPECT_EQ(result.reduced_to_original, (std::vector<QubitId>{0, 1, 2}));
    EXPECT_EQ(result.reduced_circuit.operations().size(), 3U);
}

// A gate applied AFTER the causally-relevant history, on a qubit not in
// the support, must be dropped -- it cannot affect the requested
// output's light cone even though it touches a qubit the cone later
// reaches through an earlier operation.
TEST(LightConeTest, OperationAfterTheRelevantHistoryIsDropped) {
    Circuit circuit(2, 0);
    circuit.append(Opcode::H, {0});
    circuit.append(Opcode::CX, {0, 1});
    circuit.append(Opcode::X, {1});  // after the light cone's relevant history

    const LightConeResult result = compute_light_cone(circuit, {0});
    ASSERT_EQ(result.reduced_circuit.operations().size(), 2U);
    const auto& op0 = std::get<GateOp>(result.reduced_circuit.operations()[0]);
    const auto& op1 = std::get<GateOp>(result.reduced_circuit.operations()[1]);
    EXPECT_EQ(op0.opcode, Opcode::H);
    EXPECT_EQ(op1.opcode, Opcode::CX);
}

// Two independent halves, an observable/counts subset local to one half:
// the reduced circuit must contain zero operations from the other half
// (docs/BUILD_PLAN.md Phase 5 acceptance criterion 2).
TEST(LightConeTest, TwoIndependentHalvesShowZeroCrossContamination) {
    Circuit circuit(4, 0);
    circuit.append(Opcode::H, {0});
    circuit.append(Opcode::CX, {0, 1});
    circuit.append(Opcode::H, {2});
    circuit.append(Opcode::CX, {2, 3});

    const LightConeResult result = compute_light_cone(circuit, {0});
    EXPECT_EQ(result.reduced_to_original, (std::vector<QubitId>{0, 1}));
    ASSERT_EQ(result.reduced_circuit.operations().size(), 2U);
    for (const Operation& op : result.reduced_circuit.operations()) {
        const auto& gate = std::get<GateOp>(op);
        for (QubitId q : gate.qubits) {
            EXPECT_LT(q, 2U) << "no operand may come from the other, unrelated half";
        }
    }
}

TEST(LightConeTest, GlobalPhaseIsDropped) {
    Circuit circuit(1, 0);
    circuit.global_phase = 1.23;
    circuit.append(Opcode::X, {0});

    const LightConeResult result = compute_light_cone(circuit, {0});
    EXPECT_EQ(result.reduced_circuit.global_phase, 0.0);
}

TEST(LightConeTest, MeasureOpKeepsItsOwnClbitUnremapped) {
    Circuit circuit(3, 3);
    circuit.append(Opcode::CX, {1, 2});
    circuit.append_measure(2, 2);  // global qubit 2 -> local qubit 1; clbit untouched

    const LightConeResult result = compute_light_cone(circuit, {2});
    EXPECT_EQ(result.reduced_to_original, (std::vector<QubitId>{1, 2}));
    bool found = false;
    for (const Operation& op : result.reduced_circuit.operations()) {
        if (const auto* measure = std::get_if<MeasureOp>(&op)) {
            EXPECT_EQ(measure->qubit, 1U);  // global 2 -> local 1
            EXPECT_EQ(measure->clbit, 2U);  // untouched, global namespace
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(LightConeTest, QubitOutsideEveryOperationsSupportIsNotInTheReducedCircuit) {
    Circuit circuit(3, 0);
    circuit.append(Opcode::X, {0});
    // qubit 1 is fully idle and not in the support: must not appear.
    const LightConeResult result = compute_light_cone(circuit, {0});
    EXPECT_EQ(result.reduced_to_original, (std::vector<QubitId>{0}));
}

// A channel is in the cone exactly like a gate (Phase 12, docs/adr/0016
// section 5): one touching the cone is kept, with its Kraus operators and its
// qubits remapped, and pulls its qubits in; one acting only outside the cone is
// dropped, which is exact because every channel is trace preserving.
TEST(LightConeTest, AChannelTouchingTheConeIsKeptAndOneOutsideItIsDropped) {
    const std::vector<std::vector<std::complex<double>>> kraus = {{1.0, 0.0, 0.0, 1.0}};
    Circuit circuit(3, 0);
    circuit.append_channel({0}, kraus);  // outside: qubit 0 never reaches the cone
    circuit.append(Opcode::CX, {1, 2});
    circuit.append_channel({1}, kraus);  // after the CX, on a qubit outside the support
    circuit.append_channel({2}, kraus);  // in the cone

    const LightConeResult result = compute_light_cone(circuit, {2});
    EXPECT_EQ(result.reduced_to_original, (std::vector<QubitId>{1, 2}));
    ASSERT_EQ(result.reduced_circuit.operations().size(), 2U);
    EXPECT_TRUE(std::holds_alternative<GateOp>(result.reduced_circuit.operations()[0]));
    const auto& channel = std::get<ChannelOp>(result.reduced_circuit.operations()[1]);
    EXPECT_EQ(channel.qubits, (std::vector<QubitId>{1}));  // global 2 -> local 1
    EXPECT_EQ(channel.kraus, kraus);
}

// A channel before the relevant history pulls its qubits into the cone like a
// two-qubit gate would.
TEST(LightConeTest, ATwoQubitChannelPullsItsQubitsIntoTheCone) {
    std::vector<std::complex<double>> identity_4x4(16, 0.0);
    for (std::size_t i = 0; i < 4; ++i) {
        identity_4x4[i * 4 + i] = 1.0;
    }
    Circuit circuit(2, 0);
    circuit.append(Opcode::H, {0});
    circuit.append_channel({0, 1}, {identity_4x4});

    const LightConeResult result = compute_light_cone(circuit, {1});
    EXPECT_EQ(result.reduced_to_original, (std::vector<QubitId>{0, 1}));
    EXPECT_EQ(result.reduced_circuit.operations().size(), 2U);
}

// The reduced circuit is what the engines run, so a measurement's readout
// error must survive the remap; dropping it would silently remove noise.
TEST(LightConeTest, AMeasurementKeepsItsReadoutError) {
    Circuit circuit(2, 2);
    circuit.append(Opcode::X, {1});
    circuit.append_measure(1, 1, ReadoutError{{0.9, 0.1, 0.2, 0.8}});

    const LightConeResult result = compute_light_cone(circuit, {1});
    ASSERT_EQ(result.reduced_circuit.operations().size(), 2U);
    const auto& measure = std::get<MeasureOp>(result.reduced_circuit.operations()[1]);
    EXPECT_EQ(measure.qubit, 0U);
    ASSERT_TRUE(measure.readout_error.has_value());
    // Guarded by the ASSERT_TRUE above; clang-tidy cannot see through it.
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    EXPECT_EQ(measure.readout_error.value().assignment,
              (std::array<double, 4>{0.9, 0.1, 0.2, 0.8}));
}

}  // namespace
}  // namespace menos::analysis
