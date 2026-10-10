#include "menos/circuit.h"

#include <gtest/gtest.h>

#include "ir/depth.h"

namespace menos {
namespace {

TEST(CircuitTest, StoresQubitAndClbitCounts) {
    const Circuit circuit(3, 2);
    EXPECT_EQ(circuit.num_qubits(), 3U);
    EXPECT_EQ(circuit.num_clbits(), 2U);
    EXPECT_EQ(circuit.global_phase, 0.0);
}

TEST(CircuitTest, AppendAddsGateOp) {
    Circuit circuit(2, 0);
    circuit.append(Opcode::H, {0});
    circuit.append(Opcode::CX, {0, 1});
    ASSERT_EQ(circuit.operations().size(), 2U);

    const auto& first = std::get<GateOp>(circuit.operations()[0]);
    EXPECT_EQ(first.opcode, Opcode::H);
    EXPECT_EQ(first.qubits, (std::vector<QubitId>{0}));
    EXPECT_TRUE(first.params.empty());

    const auto& second = std::get<GateOp>(circuit.operations()[1]);
    EXPECT_EQ(second.opcode, Opcode::CX);
    EXPECT_EQ(second.qubits, (std::vector<QubitId>{0, 1}));
}

TEST(CircuitTest, AppendWithParams) {
    Circuit circuit(1, 0);
    circuit.append(Opcode::RX, {0}, {1.5});
    const auto& op = std::get<GateOp>(circuit.operations()[0]);
    EXPECT_EQ(op.params, (std::vector<double>{1.5}));
}

TEST(CircuitTest, AppendUnitaryAddsUnitaryOp) {
    Circuit circuit(1, 0);
    const std::vector<std::complex<double>> matrix{0.0, 1.0, 1.0, 0.0};
    circuit.append_unitary({0}, matrix);
    const auto& op = std::get<UnitaryOp>(circuit.operations()[0]);
    EXPECT_EQ(op.qubits, (std::vector<QubitId>{0}));
    EXPECT_EQ(op.matrix, matrix);
}

TEST(CircuitTest, AppendMeasureAddsMeasureOp) {
    Circuit circuit(1, 1);
    circuit.append_measure(0, 0);
    const auto& op = std::get<MeasureOp>(circuit.operations()[0]);
    EXPECT_EQ(op.qubit, 0U);
    EXPECT_EQ(op.clbit, 0U);
}

TEST(DepthTest, EmptyCircuitHasZeroDepth) {
    const Circuit circuit(2, 0);
    EXPECT_EQ(ir::depth(circuit), 0);
}

TEST(DepthTest, SequentialOpsOnOneQubitChain) {
    Circuit circuit(1, 0);
    circuit.append(Opcode::X, {0});
    circuit.append(Opcode::X, {0});
    circuit.append(Opcode::X, {0});
    EXPECT_EQ(ir::depth(circuit), 3);
}

TEST(DepthTest, IndependentQubitsDoNotAddDepth) {
    Circuit circuit(2, 0);
    circuit.append(Opcode::X, {0});
    circuit.append(Opcode::X, {1});
    EXPECT_EQ(ir::depth(circuit), 1);
}

TEST(DepthTest, TwoQubitGateMergesChains) {
    // q0: H, q1 idle; then CX(0,1); then X on q1 only.
    Circuit circuit(2, 0);
    circuit.append(Opcode::H, {0});      // depth[q0]=1
    circuit.append(Opcode::CX, {0, 1});  // depends on q0(1) and q1(0) -> depth 2 for both
    circuit.append(Opcode::X, {1});      // depth[q1] = 3
    EXPECT_EQ(ir::depth(circuit), 3);
}

TEST(DepthTest, MeasurementCounts) {
    Circuit circuit(1, 1);
    circuit.append(Opcode::X, {0});
    circuit.append_measure(0, 0);
    EXPECT_EQ(ir::depth(circuit), 2);
}

}  // namespace
}  // namespace menos
