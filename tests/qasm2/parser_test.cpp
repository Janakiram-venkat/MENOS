#include "menos/qasm2.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>
#include <variant>

#include "common/error.h"
#include "ir/validate.h"

namespace menos::qasm2 {
namespace {

TEST(Qasm2ParserTest, ParsesQregAndSimpleGates) {
    const Circuit circuit = parse(R"(
        OPENQASM 2.0;
        include "qelib1.inc";
        qreg q[2];
        h q[0];
        cx q[0],q[1];
    )");

    EXPECT_EQ(circuit.num_qubits(), 2U);
    EXPECT_EQ(circuit.num_clbits(), 0U);
    ASSERT_EQ(circuit.operations().size(), 2U);

    const auto& op0 = std::get<GateOp>(circuit.operations()[0]);
    EXPECT_EQ(op0.opcode, Opcode::H);
    EXPECT_EQ(op0.qubits, std::vector<QubitId>{0});

    const auto& op1 = std::get<GateOp>(circuit.operations()[1]);
    EXPECT_EQ(op1.opcode, Opcode::CX);
    EXPECT_EQ(op1.qubits, (std::vector<QubitId>{0, 1}));
}

TEST(Qasm2ParserTest, ParsesCregAndMeasure) {
    const Circuit circuit = parse(R"(
        OPENQASM 2.0;
        include "qelib1.inc";
        qreg q[2];
        creg c[2];
        h q[0];
        measure q[0] -> c[0];
        measure q[1] -> c[1];
    )");

    EXPECT_EQ(circuit.num_qubits(), 2U);
    EXPECT_EQ(circuit.num_clbits(), 2U);
    ASSERT_EQ(circuit.operations().size(), 3U);
    const auto& m0 = std::get<MeasureOp>(circuit.operations()[1]);
    EXPECT_EQ(m0.qubit, 0U);
    EXPECT_EQ(m0.clbit, 0U);
    const auto& m1 = std::get<MeasureOp>(circuit.operations()[2]);
    EXPECT_EQ(m1.qubit, 1U);
    EXPECT_EQ(m1.clbit, 1U);
}

TEST(Qasm2ParserTest, ParsesPlainAndNegativeFloatParameters) {
    const Circuit circuit = parse(R"(
        OPENQASM 2.0;
        include "qelib1.inc";
        qreg q[1];
        rz(-3.4263965948628488) q[0];
        u(1.5,-2.1009221439899983,0.5) q[0];
    )");

    const auto& rz = std::get<GateOp>(circuit.operations()[0]);
    EXPECT_EQ(rz.opcode, Opcode::RZ);
    ASSERT_EQ(rz.params.size(), 1U);
    EXPECT_DOUBLE_EQ(rz.params[0], -3.4263965948628488);

    const auto& u = std::get<GateOp>(circuit.operations()[1]);
    ASSERT_EQ(u.params.size(), 3U);
    EXPECT_DOUBLE_EQ(u.params[0], 1.5);
    EXPECT_DOUBLE_EQ(u.params[1], -2.1009221439899983);
    EXPECT_DOUBLE_EQ(u.params[2], 0.5);
}

// The small standard expression grammar (pi, unary +/-, binary +-*/,
// parens) real hand-written OpenQASM 2 commonly uses for angles.
TEST(Qasm2ParserTest, ParsesPiArithmeticExpressions) {
    const Circuit circuit = parse(R"(
        OPENQASM 2.0;
        include "qelib1.inc";
        qreg q[1];
        rz(pi/2) q[0];
        rz(-pi/4) q[0];
        rz(pi/2 + pi/4) q[0];
        rz(2*pi) q[0];
        rz(-(pi/2)) q[0];
    )");

    ASSERT_EQ(circuit.operations().size(), 5U);
    constexpr double kPi = std::numbers::pi_v<double>;
    EXPECT_DOUBLE_EQ(std::get<GateOp>(circuit.operations()[0]).params[0], kPi / 2);
    EXPECT_DOUBLE_EQ(std::get<GateOp>(circuit.operations()[1]).params[0], -kPi / 4);
    EXPECT_DOUBLE_EQ(std::get<GateOp>(circuit.operations()[2]).params[0], kPi / 2 + kPi / 4);
    EXPECT_DOUBLE_EQ(std::get<GateOp>(circuit.operations()[3]).params[0], 2 * kPi);
    EXPECT_DOUBLE_EQ(std::get<GateOp>(circuit.operations()[4]).params[0], -(kPi / 2));
}

TEST(Qasm2ParserTest, BarrierAndIdAreDroppedProducingNoOperation) {
    const Circuit circuit = parse(R"(
        OPENQASM 2.0;
        include "qelib1.inc";
        qreg q[2];
        h q[0];
        barrier q[0],q[1];
        id q[1];
        cx q[0],q[1];
    )");

    ASSERT_EQ(circuit.operations().size(), 2U);
    EXPECT_EQ(std::get<GateOp>(circuit.operations()[0]).opcode, Opcode::H);
    EXPECT_EQ(std::get<GateOp>(circuit.operations()[1]).opcode, Opcode::CX);
}

TEST(Qasm2ParserTest, CommentsAreIgnored) {
    const Circuit circuit = parse(R"(
        // a leading comment
        OPENQASM 2.0; // trailing comment
        include "qelib1.inc";
        qreg q[1]; // one qubit
        h q[0]; // hadamard
    )");
    EXPECT_EQ(circuit.num_qubits(), 1U);
    ASSERT_EQ(circuit.operations().size(), 1U);
}

// --- Failure paths: each must throw the documented exception type, and
// the message must name the offending line. ---

TEST(Qasm2ParserTest, MissingIncludeThrowsUnsupportedFeature) {
    try {
        static_cast<void>(parse("OPENQASM 2.0;\nqreg q[1];\nh q[0];\n"));
        FAIL() << "expected UnsupportedFeature";
    } catch (const common::UnsupportedFeature& e) {
        EXPECT_NE(std::string(e.what()).find("include"), std::string::npos) << e.what();
    }
}

TEST(Qasm2ParserTest, WrongVersionThrowsUnsupportedFeature) {
    try {
        static_cast<void>(parse("OPENQASM 3.0;\ninclude \"qelib1.inc\";\nqreg q[1];\n"));
        FAIL() << "expected UnsupportedFeature";
    } catch (const common::UnsupportedFeature& e) {
        EXPECT_NE(std::string(e.what()).find("line 1"), std::string::npos) << e.what();
    }
}

TEST(Qasm2ParserTest, GateBlockThrowsUnsupportedFeatureNamingTheLine) {
    const std::string text =
        "OPENQASM 2.0;\n"
        "include \"qelib1.inc\";\n"
        "qreg q[1];\n"
        "gate foo a { h a; }\n";
    try {
        static_cast<void>(parse(text));
        FAIL() << "expected UnsupportedFeature";
    } catch (const common::UnsupportedFeature& e) {
        EXPECT_NE(std::string(e.what()).find("line 4"), std::string::npos) << e.what();
    }
}

TEST(Qasm2ParserTest, IfThrowsUnsupportedFeatureNamingTheLine) {
    const std::string text =
        "OPENQASM 2.0;\n"
        "include \"qelib1.inc\";\n"
        "qreg q[1];\n"
        "creg c[1];\n"
        "if (c==1) h q[0];\n";
    try {
        static_cast<void>(parse(text));
        FAIL() << "expected UnsupportedFeature";
    } catch (const common::UnsupportedFeature& e) {
        EXPECT_NE(std::string(e.what()).find("line 5"), std::string::npos) << e.what();
    }
}

TEST(Qasm2ParserTest, OpaqueThrowsUnsupportedFeatureNamingTheLine) {
    const std::string text =
        "OPENQASM 2.0;\n"
        "include \"qelib1.inc\";\n"
        "qreg q[1];\n"
        "opaque foo(theta) a;\n";
    try {
        static_cast<void>(parse(text));
        FAIL() << "expected UnsupportedFeature";
    } catch (const common::UnsupportedFeature& e) {
        EXPECT_NE(std::string(e.what()).find("line 4"), std::string::npos) << e.what();
    }
}

TEST(Qasm2ParserTest, ResetThrowsUnsupportedFeatureNamingTheLine) {
    const std::string text =
        "OPENQASM 2.0;\n"
        "include \"qelib1.inc\";\n"
        "qreg q[1];\n"
        "reset q[0];\n";
    try {
        static_cast<void>(parse(text));
        FAIL() << "expected UnsupportedFeature";
    } catch (const common::UnsupportedFeature& e) {
        EXPECT_NE(std::string(e.what()).find("line 4"), std::string::npos) << e.what();
    }
}

TEST(Qasm2ParserTest, UnknownGateNameThrowsUnsupportedFeatureNamingTheLine) {
    // cy is a real qelib1.inc gate with no MENOS opcode.
    const std::string text =
        "OPENQASM 2.0;\n"
        "include \"qelib1.inc\";\n"
        "qreg q[2];\n"
        "cy q[0],q[1];\n";
    try {
        static_cast<void>(parse(text));
        FAIL() << "expected UnsupportedFeature";
    } catch (const common::UnsupportedFeature& e) {
        EXPECT_NE(std::string(e.what()).find("cy"), std::string::npos) << e.what();
        EXPECT_NE(std::string(e.what()).find("line 4"), std::string::npos) << e.what();
    }
}

TEST(Qasm2ParserTest, MalformedFileThrowsInvalidCircuit) {
    // Missing the closing ';' after the gate statement.
    const std::string text =
        "OPENQASM 2.0;\n"
        "include \"qelib1.inc\";\n"
        "qreg q[1];\n"
        "h q[0]\n";
    EXPECT_THROW(static_cast<void>(parse(text)), common::InvalidCircuit);
}

TEST(Qasm2ParserTest, UndeclaredRegisterThrowsInvalidCircuit) {
    const std::string text =
        "OPENQASM 2.0;\n"
        "include \"qelib1.inc\";\n"
        "qreg q[1];\n"
        "h r[0];\n";
    EXPECT_THROW(static_cast<void>(parse(text)), common::InvalidCircuit);
}

TEST(Qasm2ParserTest, OutOfRangeQubitIndexThrowsInvalidCircuit) {
    const std::string text =
        "OPENQASM 2.0;\n"
        "include \"qelib1.inc\";\n"
        "qreg q[1];\n"
        "h q[1];\n";
    EXPECT_THROW(static_cast<void>(parse(text)), common::InvalidCircuit);
}

TEST(Qasm2ParserTest, WrongOperandCountThrowsInvalidCircuit) {
    const std::string text =
        "OPENQASM 2.0;\n"
        "include \"qelib1.inc\";\n"
        "qreg q[2];\n"
        "h q[0],q[1];\n";  // h takes exactly 1 qubit
    EXPECT_THROW(static_cast<void>(parse(text)), common::InvalidCircuit);
}

TEST(Qasm2ParserTest, WrongParamCountThrowsInvalidCircuit) {
    const std::string text =
        "OPENQASM 2.0;\n"
        "include \"qelib1.inc\";\n"
        "qreg q[1];\n"
        "rz q[0];\n";  // rz takes exactly 1 parameter
    EXPECT_THROW(static_cast<void>(parse(text)), common::InvalidCircuit);
}

TEST(Qasm2ParserTest, MultipleQregThrowsUnsupportedFeature) {
    const std::string text =
        "OPENQASM 2.0;\n"
        "include \"qelib1.inc\";\n"
        "qreg q[1];\n"
        "qreg r[1];\n";
    EXPECT_THROW(static_cast<void>(parse(text)), common::UnsupportedFeature);
}

TEST(Qasm2ParserTest, MultipleCregThrowsUnsupportedFeature) {
    const std::string text =
        "OPENQASM 2.0;\n"
        "include \"qelib1.inc\";\n"
        "qreg q[1];\n"
        "creg c[1];\n"
        "creg d[1];\n";
    EXPECT_THROW(static_cast<void>(parse(text)), common::UnsupportedFeature);
}

TEST(Qasm2ParserTest, NoQregDeclarationThrowsInvalidCircuit) {
    const std::string text = "OPENQASM 2.0;\ninclude \"qelib1.inc\";\n";
    EXPECT_THROW(static_cast<void>(parse(text)), common::InvalidCircuit);
}

TEST(Qasm2ParserTest, MeasureToUndeclaredCregThrowsInvalidCircuit) {
    const std::string text =
        "OPENQASM 2.0;\n"
        "include \"qelib1.inc\";\n"
        "qreg q[1];\n"
        "measure q[0] -> c[0];\n";
    EXPECT_THROW(static_cast<void>(parse(text)), common::InvalidCircuit);
}

// docs/adr/0023 section 9, Q5: measurements keep their source position.
// The parameters on both sides of the measure check that each batch's
// offsets are rebased correctly.
TEST(Qasm2ParserTest, MeasurementsKeepSourceOrder) {
    const Circuit circuit = parse(R"(
        OPENQASM 2.0;
        include "qelib1.inc";
        qreg q[2];
        creg c[2];
        rx(0.25) q[0];
        measure q[0] -> c[0];
        cp(0.5) q[1],q[0];
        ry(-0.75) q[1];
        measure q[1] -> c[1];
    )");

    ASSERT_EQ(circuit.operations().size(), 5U);
    const auto& rx = std::get<GateOp>(circuit.operations()[0]);
    EXPECT_EQ(rx.opcode, Opcode::RX);
    EXPECT_EQ(rx.params, std::vector<double>{0.25});
    EXPECT_EQ(std::get<MeasureOp>(circuit.operations()[1]).qubit, 0U);
    const auto& cp = std::get<GateOp>(circuit.operations()[2]);
    EXPECT_EQ(cp.opcode, Opcode::CP);
    EXPECT_EQ(cp.qubits, (std::vector<QubitId>{1, 0}));
    EXPECT_EQ(cp.params, std::vector<double>{0.5});
    const auto& ry = std::get<GateOp>(circuit.operations()[3]);
    EXPECT_EQ(ry.opcode, Opcode::RY);
    EXPECT_EQ(ry.qubits, std::vector<QubitId>{1});
    EXPECT_EQ(ry.params, std::vector<double>{-0.75});
    EXPECT_EQ(std::get<MeasureOp>(circuit.operations()[4]).qubit, 1U);
}

// Before Q5 the x below was moved ahead of the measurement, silently
// changing the result; now validate() refuses it as it refuses every
// operation on an already-measured qubit.
TEST(Qasm2ParserTest, GateAfterMeasuringTheSameQubitIsRefusedByValidate) {
    const Circuit circuit = parse(R"(
        OPENQASM 2.0;
        include "qelib1.inc";
        qreg q[1];
        creg c[1];
        measure q[0] -> c[0];
        x q[0];
    )");

    ASSERT_EQ(circuit.operations().size(), 2U);
    EXPECT_TRUE(std::holds_alternative<MeasureOp>(circuit.operations()[0]));
    EXPECT_THROW(ir::validate(circuit), common::UnsupportedFeature);
}

}  // namespace
}  // namespace menos::qasm2
