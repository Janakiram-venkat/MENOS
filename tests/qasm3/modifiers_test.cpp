// Gate modifiers (docs/adr/0023 section 3, gate criterion 4): each rule's
// result, compared entrywise with a matrix built independently here from the
// gate table, global phase included.

#include <gtest/gtest.h>

#include <complex>
#include <numbers>
#include <string>
#include <variant>

#include "common/error.h"
#include "common/tolerance.h"
#include "menos/qasm3.h"
#include "unitary_of.h"

namespace menos::qasm3 {
namespace {

using testing::Complex;
using testing::controlled_reference;
using testing::Matrix;
using testing::max_abs_difference;
using testing::unitary_of;

constexpr double kTol = common::kDefaultAbsTol;

Circuit program(const std::string& body, std::uint32_t num_qubits) {
    return parse("OPENQASM 3.0;\ninclude \"stdgates.inc\";\nqubit[" + std::to_string(num_qubits) +
                 "] q;\n" + body);
}

Matrix table(Opcode opcode, std::vector<double> params = {}) {
    return ir::generate_matrix(opcode, params);
}

Matrix dagger(const Matrix& m, std::size_t dim) {
    Matrix out(m.size());
    for (std::size_t r = 0; r < dim; ++r) {
        for (std::size_t c = 0; c < dim; ++c) {
            out[c * dim + r] = std::conj(m[r * dim + c]);
        }
    }
    return out;
}

TEST(Qasm3ModifiersTest, InvOfATableGateIsItsTableInverse) {
    const Circuit circuit = program("inv @ s q[0];\ninv @ sx q[0];\n", 1);
    ASSERT_EQ(circuit.operations().size(), 2U);
    EXPECT_EQ(std::get<GateOp>(circuit.operations()[0]).opcode, Opcode::Sdg);
    EXPECT_EQ(std::get<GateOp>(circuit.operations()[1]).opcode, Opcode::SXdg);
}

TEST(Qasm3ModifiersTest, InvOfUIsTheAdjoint) {
    const Circuit circuit = program("inv @ U(0.3, -1.1, 2.2) q[0];\n", 1);
    const Matrix expected = dagger(table(Opcode::U, {0.3, -1.1, 2.2}), 2);
    EXPECT_LE(max_abs_difference(unitary_of(circuit), expected), kTol);
}

TEST(Qasm3ModifiersTest, InvOfAUserGateReversesAndInvertsItsBody) {
    const Circuit circuit =
        program("gate g(a) x, y { h x; cx x, y; rz(a) y; t y; }\ninv @ g(0.7) q[0], q[1];\n", 2);
    const Circuit forward = program(
        "gate g(a) x, y { h x; cx x, y; rz(a) y; t y; }\n"
        "g(0.7) q[0], q[1];\n",
        2);
    EXPECT_LE(max_abs_difference(unitary_of(circuit), dagger(unitary_of(forward), 4)), kTol);
}

TEST(Qasm3ModifiersTest, PowRepeatsAndNegativePowInverts) {
    const Matrix t = table(Opcode::T);
    const Matrix t3 = testing::multiply(testing::multiply(t, t, 2), t, 2);
    EXPECT_LE(max_abs_difference(unitary_of(program("pow(3) @ t q[0];\n", 1)), t3), kTol);

    const Matrix sxdg = dagger(table(Opcode::SX), 2);
    EXPECT_LE(max_abs_difference(unitary_of(program("pow(-2) @ sx q[0];\n", 1)),
                                 testing::multiply(sxdg, sxdg, 2)),
              kTol);
    EXPECT_TRUE(program("pow(0) @ h q[0];\n", 1).operations().empty());
}

TEST(Qasm3ModifiersTest, CtrlOfXZAndPUsesTheTableGates) {
    const Circuit circuit = program(
        "ctrl @ x q[0], q[1];\nctrl @ ctrl @ x q[0], q[1], q[2];\nctrl(2) @ x q[2], q[1], q[0];\n"
        "ctrl @ z q[1], q[2];\nctrl @ p(0.4) q[2], q[0];\n",
        3);
    ASSERT_EQ(circuit.operations().size(), 5U);
    const auto& cx = std::get<GateOp>(circuit.operations()[0]);
    EXPECT_EQ(cx.opcode, Opcode::CX);
    EXPECT_EQ(cx.qubits, (std::vector<QubitId>{0, 1}));
    const auto& ccx = std::get<GateOp>(circuit.operations()[1]);
    EXPECT_EQ(ccx.opcode, Opcode::CCX);
    EXPECT_EQ(ccx.qubits, (std::vector<QubitId>{0, 1, 2}));
    EXPECT_EQ(std::get<GateOp>(circuit.operations()[2]).qubits, (std::vector<QubitId>{2, 1, 0}));
    EXPECT_EQ(std::get<GateOp>(circuit.operations()[3]).opcode, Opcode::CZ);
    const auto& cp = std::get<GateOp>(circuit.operations()[4]);
    EXPECT_EQ(cp.opcode, Opcode::CP);
    EXPECT_EQ(cp.params, std::vector<double>{0.4});
}

TEST(Qasm3ModifiersTest, CtrlOfGphaseIsAPhaseOnTheControl) {
    const Circuit circuit = program("ctrl @ gphase(0.9) q[0];\n", 1);
    ASSERT_EQ(circuit.operations().size(), 1U);
    const auto& p = std::get<GateOp>(circuit.operations()[0]);
    EXPECT_EQ(p.opcode, Opcode::P);
    EXPECT_EQ(p.params, std::vector<double>{0.9});
    EXPECT_EQ(circuit.global_phase, 0.0);
}

TEST(Qasm3ModifiersTest, CtrlOfAGateWithoutATableFormIsAnExactUnitary) {
    struct Case {
        std::string text;
        Matrix gate;
    };
    const Case cases[] = {
        {"ctrl @ h q[1], q[0];\n", table(Opcode::H)},
        {"ctrl @ ry(0.25) q[1], q[0];\n", table(Opcode::RY, {0.25})},
        {"ctrl @ U(0.1, 0.2, 0.3) q[1], q[0];\n", table(Opcode::U, {0.1, 0.2, 0.3})},
        {"ctrl @ sx q[1], q[0];\n", table(Opcode::SX)},
    };
    for (const Case& c : cases) {
        const Circuit circuit = program(c.text, 2);
        ASSERT_EQ(circuit.operations().size(), 1U) << c.text;
        EXPECT_TRUE(std::holds_alternative<UnitaryOp>(circuit.operations()[0])) << c.text;
        const Matrix expected = controlled_reference(c.gate, {1}, {true}, {0}, 2);
        EXPECT_LE(max_abs_difference(unitary_of(circuit), expected), kTol) << c.text;
    }
}

TEST(Qasm3ModifiersTest, CtrlOfAGateWithAGlobalPhaseMakesThePhaseRelative) {
    // g = e^{i 0.6} H. Uncontrolled the phase is global; controlled it is not.
    const std::string def = "gate g a { gphase(0.6); h a; }\n";
    Matrix phased_h = table(Opcode::H);
    for (Complex& entry : phased_h) {
        entry *= std::polar(1.0, 0.6);
    }
    const Circuit controlled = program(def + "ctrl @ g q[0], q[1];\n", 2);
    EXPECT_LE(max_abs_difference(unitary_of(controlled),
                                 controlled_reference(phased_h, {0}, {true}, {1}, 2)),
              kTol);
}

TEST(Qasm3ModifiersTest, NegctrlActsWhenTheControlIsZero) {
    const Circuit circuit =
        program("negctrl @ x q[0], q[1];\nnegctrl(2) @ h q[2], q[0], q[1];\n", 3);
    const Matrix first = controlled_reference(table(Opcode::X), {0}, {false}, {1}, 3);
    const Matrix second = controlled_reference(table(Opcode::H), {2, 0}, {false, false}, {1}, 3);
    EXPECT_LE(max_abs_difference(unitary_of(circuit), testing::multiply(second, first, 8)), kTol);
}

TEST(Qasm3ModifiersTest, MixedCtrlAndNegctrlTakeTheirControlsLeftToRight) {
    const Circuit circuit = program("ctrl @ negctrl @ rx(1.3) q[0], q[1], q[2];\n", 3);
    const Matrix expected =
        controlled_reference(table(Opcode::RX, {1.3}), {0, 1}, {true, false}, {2}, 3);
    EXPECT_LE(max_abs_difference(unitary_of(circuit), expected), kTol);
}

TEST(Qasm3ModifiersTest, CtrlOfInvOfPowComposesInsideOut) {
    const Circuit circuit = program("ctrl @ inv @ pow(2) @ t q[1], q[0];\n", 2);
    const Matrix sdg = dagger(table(Opcode::S), 2);  // (T^2)^dagger = S^dagger
    EXPECT_LE(
        max_abs_difference(unitary_of(circuit), controlled_reference(sdg, {1}, {true}, {0}, 2)),
        kTol);
}

TEST(Qasm3ModifiersTest, AControlledUnitaryOfFiveQubitsIsAcceptedAndOfSixRefused) {
    const Circuit five = program("ctrl(4) @ h q[0], q[1], q[2], q[3], q[4];\n", 5);
    ASSERT_EQ(five.operations().size(), 1U);
    EXPECT_EQ(std::get<UnitaryOp>(five.operations()[0]).qubits.size(), 5U);
    EXPECT_LE(
        max_abs_difference(
            unitary_of(five),
            controlled_reference(table(Opcode::H), {0, 1, 2, 3}, {true, true, true, true}, {4}, 5)),
        kTol);
    try {
        static_cast<void>(program("ctrl(5) @ h q[0], q[1], q[2], q[3], q[4], q[5];\n", 6));
        FAIL() << "expected UnsupportedFeature";
    } catch (const common::UnsupportedFeature& error) {
        EXPECT_NE(std::string(error.what()).find("6 qubits"), std::string::npos) << error.what();
        EXPECT_NE(std::string(error.what()).find("line 4, column 1"), std::string::npos)
            << error.what();
    }
}

}  // namespace
}  // namespace menos::qasm3
