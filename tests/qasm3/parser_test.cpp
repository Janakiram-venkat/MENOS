// The accepted OpenQASM 3 subset (docs/adr/0023 section 2.1, gate criterion 1)
// and the malformed-input errors.

#include <gtest/gtest.h>

#include <cmath>
#include <iterator>
#include <limits>
#include <numbers>
#include <string>
#include <variant>

#include "common/error.h"
#include "common/tolerance.h"
#include "ir/validate.h"
#include "menos/qasm3.h"
#include "unitary_of.h"

namespace menos::qasm3 {
namespace {

using common::InvalidCircuit;

const GateOp& gate_at(const Circuit& circuit, std::size_t i) {
    return std::get<GateOp>(circuit.operations().at(i));
}
const MeasureOp& measure_at(const Circuit& circuit, std::size_t i) {
    return std::get<MeasureOp>(circuit.operations().at(i));
}

TEST(Qasm3ParserTest, ParsesQiskitStyleProgramWithSeveralRegisters) {
    const Circuit circuit = parse(R"(OPENQASM 3.0;
include "stdgates.inc";
bit[2] c;
bit[1] d;
qubit[3] q;
h q[0];
cx q[0], q[1];
c[0] = measure q[0];
c[1] = measure q[1];
d[0] = measure q[2];
)");
    EXPECT_EQ(circuit.num_qubits(), 3U);
    EXPECT_EQ(circuit.num_clbits(), 3U);
    ASSERT_EQ(circuit.operations().size(), 5U);
    EXPECT_EQ(gate_at(circuit, 0).opcode, Opcode::H);
    EXPECT_EQ(gate_at(circuit, 1).qubits, (std::vector<QubitId>{0, 1}));
    EXPECT_EQ(measure_at(circuit, 4).qubit, 2U);
    EXPECT_EQ(measure_at(circuit, 4).clbit, 2U);  // d follows c's two bits
}

TEST(Qasm3ParserTest, RegistersFlattenInDeclarationOrder) {
    const Circuit circuit = parse(R"(include "stdgates.inc";
qubit[2] a;
qubit b;
qreg c[2];
x a[1];
x b;
x c[0];
)");
    EXPECT_EQ(circuit.num_qubits(), 5U);
    EXPECT_EQ(gate_at(circuit, 0).qubits, std::vector<QubitId>{1});
    EXPECT_EQ(gate_at(circuit, 1).qubits, std::vector<QubitId>{2});
    EXPECT_EQ(gate_at(circuit, 2).qubits, std::vector<QubitId>{3});
}

TEST(Qasm3ParserTest, HeaderIsOptionalAndBothVersionSpellingsAreAccepted) {
    EXPECT_EQ(parse("qubit q; U(0, 0, 0) q;").num_qubits(), 1U);
    EXPECT_EQ(parse("OPENQASM 3; qubit q;").num_qubits(), 1U);
    EXPECT_EQ(parse("OPENQASM 3.0; qubit q;").num_qubits(), 1U);
}

TEST(Qasm3ParserTest, BuiltinUIsTheGateTableU) {
    const Circuit circuit = parse("qubit q; U(0.1, 0.2, 0.3) q;");
    ASSERT_EQ(circuit.operations().size(), 1U);
    EXPECT_EQ(gate_at(circuit, 0).opcode, Opcode::U);
    EXPECT_EQ(gate_at(circuit, 0).params, (std::vector<double>{0.1, 0.2, 0.3}));
}

TEST(Qasm3ParserTest, GphaseAddsToTheGlobalPhase) {
    const Circuit circuit = parse("qubit q; gphase(0.25); gphase(-pi/8);");
    EXPECT_TRUE(circuit.operations().empty());
    EXPECT_DOUBLE_EQ(circuit.global_phase, 0.25 - std::numbers::pi / 8);
}

TEST(Qasm3ParserTest, StdgatesNamesNeedTheInclude) {
    EXPECT_THROW(static_cast<void>(parse("qubit q; h q;")), InvalidCircuit);
}

TEST(Qasm3ParserTest, StdgatesAliasesMapToTableGates) {
    const Circuit circuit = parse(R"(include "stdgates.inc";
qubit[2] q;
phase(0.5) q[0];
u1(0.5) q[0];
cphase(0.5) q[0], q[1];
CX q[0], q[1];
u2(0.1, 0.2) q[1];
u3(0.3, 0.1, 0.2) q[1];
id q[0];
)");
    ASSERT_EQ(circuit.operations().size(), 6U);  // id produces nothing
    EXPECT_EQ(gate_at(circuit, 0).opcode, Opcode::P);
    EXPECT_EQ(gate_at(circuit, 1).opcode, Opcode::P);
    EXPECT_EQ(gate_at(circuit, 2).opcode, Opcode::CP);
    EXPECT_EQ(gate_at(circuit, 3).opcode, Opcode::CX);
    EXPECT_EQ(gate_at(circuit, 4).opcode, Opcode::U);
    EXPECT_EQ(gate_at(circuit, 4).params, (std::vector<double>{std::numbers::pi / 2, 0.1, 0.2}));
    EXPECT_EQ(gate_at(circuit, 5).params, (std::vector<double>{0.3, 0.1, 0.2}));
}

TEST(Qasm3ParserTest, EveryStdgatesNameParsesWithItsArity) {
    const char* calls[] = {
        "p(0.1) q[0];",
        "x q[0];",
        "y q[0];",
        "z q[0];",
        "h q[0];",
        "s q[0];",
        "sdg q[0];",
        "t q[0];",
        "tdg q[0];",
        "sx q[0];",
        "rx(0.1) q[0];",
        "ry(0.1) q[0];",
        "rz(0.1) q[0];",
        "cx q[0], q[1];",
        "cy q[0], q[1];",
        "cz q[0], q[1];",
        "cp(0.1) q[0], q[1];",
        "crx(0.1) q[0], q[1];",
        "cry(0.1) q[0], q[1];",
        "crz(0.1) q[0], q[1];",
        "ch q[0], q[1];",
        "swap q[0], q[1];",
        "ccx q[0], q[1], q[2];",
        "cswap q[0], q[1], q[2];",
        "cu(0.1, 0.2, 0.3, 0.4) q[0], q[1];",
        "CX q[0], q[1];",
        "phase(0.1) q[0];",
        "cphase(0.1) q[0], q[1];",
        "id q[0];",
        "u1(0.1) q[0];",
        "u2(0.1, 0.2) q[0];",
        "u3(0.1, 0.2, 0.3) q[0];",
    };
    EXPECT_EQ(std::size(calls), 32U);
    for (const char* call : calls) {
        const Circuit circuit = parse(std::string("include \"stdgates.inc\"; qubit[3] q; ") + call);
        EXPECT_NO_THROW(ir::validate(circuit)) << call;
    }
}

TEST(Qasm3ParserTest, ConstantExpressions) {
    const Circuit circuit = parse(R"(include "stdgates.inc";
const float half = 0.5;
const int two = 2;
const angle a = π / 4;
qubit q;
rz(pi) q;
rz(tau / 2.0) q;
rz(2 ** 3) q;
rz(-two ** 2) q;
rz(sin(pi / 6) + cos(0) * ln(euler)) q;
rz(sqrt(4.0) * half - exp(0) + arctan(1) * 4) q;
rz(a) q;
rz(1.5e-1) q;
rz(.25) q;
)");
    const double expected[] = {std::numbers::pi,
                               std::numbers::pi,
                               8.0,
                               -4.0,
                               std::sin(std::numbers::pi / 6) + 1.0,
                               std::numbers::pi,
                               std::numbers::pi / 4,
                               0.15,
                               0.25};
    ASSERT_EQ(circuit.operations().size(), std::size(expected));
    for (std::size_t i = 0; i < std::size(expected); ++i) {
        EXPECT_NEAR(gate_at(circuit, i).params[0], expected[i], 1e-15) << i;
    }
}

TEST(Qasm3ParserTest, UnicodeConstantsAndTheRemainingFunctions) {
    const Circuit circuit = parse(R"(include "stdgates.inc";
qubit q;
rz(τ) q;
rz(ℯ) q;
rz(tan(0.5)) q;
rz(arcsin(0.5)) q;
rz(arccos(0.5)) q;
)");
    const double expected[] = {
        2 * std::numbers::pi, std::numbers::e, std::tan(0.5), std::asin(0.5), std::acos(0.5)};
    ASSERT_EQ(circuit.operations().size(), std::size(expected));
    for (std::size_t i = 0; i < std::size(expected); ++i) {
        EXPECT_NEAR(gate_at(circuit, i).params[0], expected[i], 1e-15) << i;
    }
}

TEST(Qasm3ParserTest, CregDeclaresClassicalBitsInDeclarationOrder) {
    const Circuit circuit = parse(R"(qubit[2] q;
creg c[2];
bit d;
c[1] = measure q[0];
measure q[1] -> d;
)");
    EXPECT_EQ(circuit.num_clbits(), 3U);
    ASSERT_EQ(circuit.operations().size(), 2U);
    EXPECT_EQ(measure_at(circuit, 0).clbit, 1U);
    EXPECT_EQ(measure_at(circuit, 1).clbit, 2U);  // d follows c's two bits
}

TEST(Qasm3ParserTest, CommentsAreIgnored) {
    const Circuit circuit = parse(R"(// a line comment
include "stdgates.inc"; /* a block
comment */ qubit q;
x q; // trailing
)");
    EXPECT_EQ(circuit.operations().size(), 1U);
}

TEST(Qasm3ParserTest, InputsAreBoundAtSubmission) {
    const Circuit circuit = parse(R"(include "stdgates.inc";
input float[64] theta;
input angle phi;
input float lambda;
qubit q;
U(theta, phi, lambda) q;
)",
                                  {{"theta", 0.5}, {"phi", -0.25}, {"lambda", 1.75}});
    EXPECT_EQ(gate_at(circuit, 0).params, (std::vector<double>{0.5, -0.25, 1.75}));
}

TEST(Qasm3ParserTest, InputBindingErrorsAreInvalidCircuit) {
    const std::string text = "input float theta; qubit q; U(theta, 0, 0) q;";
    EXPECT_THROW(static_cast<void>(parse(text)), InvalidCircuit);  // missing
    EXPECT_THROW(static_cast<void>(parse(text, {{"theta", 1.0}, {"extra", 2.0}})),
                 InvalidCircuit);  // unknown
    EXPECT_THROW(
        static_cast<void>(parse(text, {{"theta", std::numeric_limits<double>::infinity()}})),
        InvalidCircuit);
    EXPECT_THROW(static_cast<void>(parse(text, {{"theta", std::nan("")}})), InvalidCircuit);
    EXPECT_THROW(static_cast<void>(parse("qubit q;", {{"theta", 1.0}})), InvalidCircuit);
}

TEST(Qasm3ParserTest, UserGatesExpandWithTheirArguments) {
    const Circuit circuit = parse(R"(include "stdgates.inc";
gate inner(a) x { rz(a) x; }
gate outer(a, b) x, y { inner(a * 2) x; cx x, y; inner(b) y; }
qubit[2] q;
outer(0.25, 0.5) q[1], q[0];
)");
    ASSERT_EQ(circuit.operations().size(), 3U);
    EXPECT_EQ(gate_at(circuit, 0).qubits, std::vector<QubitId>{1});
    EXPECT_EQ(gate_at(circuit, 0).params, std::vector<double>{0.5});
    EXPECT_EQ(gate_at(circuit, 1).qubits, (std::vector<QubitId>{1, 0}));
    EXPECT_EQ(gate_at(circuit, 2).params, std::vector<double>{0.5});
}

TEST(Qasm3ParserTest, AGateBodyMayCallOnlyEarlierGates) {
    EXPECT_THROW(static_cast<void>(parse("gate a x { a x; } qubit q; a q;")), InvalidCircuit);
    EXPECT_THROW(
        static_cast<void>(parse("gate a x { b x; } gate b x { U(0,0,0) x; } qubit q; a q;")),
        InvalidCircuit);
}

TEST(Qasm3ParserTest, MeasureFormsAndSourceOrder) {
    const Circuit circuit = parse(R"(include "stdgates.inc";
qubit[3] q;
bit[3] c;
h q[0];
measure q[0] -> c[0];
x q[1];
c[1] = measure q[1];
bit b;
b = measure q[2];
)");
    ASSERT_EQ(circuit.operations().size(), 5U);
    EXPECT_EQ(gate_at(circuit, 0).opcode, Opcode::H);
    EXPECT_EQ(measure_at(circuit, 1).qubit, 0U);
    EXPECT_EQ(gate_at(circuit, 2).opcode, Opcode::X);
    EXPECT_EQ(measure_at(circuit, 3).clbit, 1U);
    EXPECT_EQ(measure_at(circuit, 4).clbit, 3U);
}

TEST(Qasm3ParserTest, RegisterWideMeasurement) {
    const Circuit circuit = parse("qubit[2] q; bit[2] c; c = measure q; measure q -> c;");
    ASSERT_EQ(circuit.operations().size(), 4U);
    EXPECT_EQ(measure_at(circuit, 1).qubit, 1U);
    EXPECT_EQ(measure_at(circuit, 1).clbit, 1U);
    EXPECT_THROW(static_cast<void>(parse("qubit[2] q; bit[3] c; c = measure q;")), InvalidCircuit);
}

TEST(Qasm3ParserTest, GateAfterMeasuringTheSameQubitIsRefusedByValidate) {
    const Circuit circuit = parse("include \"stdgates.inc\"; qubit q; bit c; c = measure q; x q;");
    EXPECT_THROW(ir::validate(circuit), common::UnsupportedFeature);
}

TEST(Qasm3ParserTest, BarrierProducesNoOperation) {
    const Circuit circuit = parse(R"(include "stdgates.inc";
qubit[2] q;
barrier q[0], q[1];
barrier q;
gate g x { barrier x; h x; }
g q[0];
)");
    ASSERT_EQ(circuit.operations().size(), 1U);
}

TEST(Qasm3ParserTest, MalformedProgramsNameTheirLocation) {
    struct Case {
        const char* text;
        const char* location;
    };
    const Case cases[] = {
        {"qubit q;\nfoo q;", "line 2, column 1"},              // undefined gate
        {"qubit[2] q;\nU(0,0,0) q[2];", "line 2, column 11"},  // index out of range
        {"qubit q;\nU(0,0) q;", "line 2, column 1"},           // parameter count
        {"qubit q;\nqubit q;", "line 2, column 7"},            // redefinition
        {"qubit q;\nU(0,0,0) r;", "line 2, column 10"},        // undeclared register
        {"qubit q;\nU(0,0,0) q", "line 2, column 11"},         // missing ';'
        {"qubit q;\nOPENQASM 3;", "line 2, column 1"},         // version not first
        {"qubit q; /* open", "line 1, column 10"},             // unterminated comment
        {"qubit q;\nrz(1/2) q;", "line 2, column 5"},          // (unsupported, see refusals)
    };
    for (const Case& c : cases) {
        try {
            static_cast<void>(parse(c.text));
            ADD_FAILURE() << "accepted: " << c.text;
        } catch (const common::Error& error) {
            EXPECT_NE(std::string(error.what()).find(c.location), std::string::npos)
                << c.text << " -> " << error.what();
        }
    }
}

TEST(Qasm3ParserTest, NoQubitIsInvalid) {
    EXPECT_THROW(static_cast<void>(parse("OPENQASM 3.0;")), InvalidCircuit);
}

}  // namespace
}  // namespace menos::qasm3
