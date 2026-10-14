// OpenQASM 2/3 equivalence (docs/adr/0023 section 5, gate criterion 6): a
// program written in both languages gives identical circuits from
// qasm2::parse and qasm3::parse, operation for operation.

#include <gtest/gtest.h>

#include <string>
#include <variant>

#include "menos/qasm2.h"
#include "menos/qasm3.h"

namespace menos::qasm3 {
namespace {

bool same_operation(const Operation& a, const Operation& b) {
    if (a.index() != b.index()) {
        return false;
    }
    if (const auto* ga = std::get_if<GateOp>(&a)) {
        const auto& gb = std::get<GateOp>(b);
        return ga->opcode == gb.opcode && ga->qubits == gb.qubits && ga->params == gb.params;
    }
    if (const auto* ma = std::get_if<MeasureOp>(&a)) {
        const auto& mb = std::get<MeasureOp>(b);
        return ma->qubit == mb.qubit && ma->clbit == mb.clbit &&
               ma->readout_error.has_value() == mb.readout_error.has_value();
    }
    return false;  // neither importer produces any other operation for these fixtures
}

void expect_identical(const Circuit& a, const Circuit& b, const std::string& name) {
    EXPECT_EQ(a.num_qubits(), b.num_qubits()) << name;
    EXPECT_EQ(a.num_clbits(), b.num_clbits()) << name;
    EXPECT_EQ(a.global_phase, b.global_phase) << name;
    ASSERT_EQ(a.operations().size(), b.operations().size()) << name;
    for (std::size_t i = 0; i < a.operations().size(); ++i) {
        EXPECT_TRUE(same_operation(a.operations()[i], b.operations()[i]))
            << name << ": operation " << i;
    }
}

struct Pair {
    const char* name;
    const char* qasm2;
    const char* qasm3;
};

const Pair kPairs[] = {
    {"bell",
     R"(OPENQASM 2.0;
include "qelib1.inc";
qreg q[2];
creg c[2];
h q[0];
cx q[0],q[1];
measure q[0] -> c[0];
measure q[1] -> c[1];
)",
     R"(OPENQASM 3.0;
include "stdgates.inc";
qubit[2] q;
bit[2] c;
h q[0];
cx q[0], q[1];
c[0] = measure q[0];
c[1] = measure q[1];
)"},
    {"every shared gate",
     R"(OPENQASM 2.0;
include "qelib1.inc";
qreg q[3];
x q[0]; y q[1]; z q[2]; h q[0]; s q[1]; sdg q[2]; t q[0]; tdg q[1]; sx q[2];
rx(pi/2) q[0]; ry(-0.25) q[1]; rz(2*pi/3) q[2]; p(1.5e-1) q[0];
u(0.1,0.2,0.3) q[1];
cx q[0],q[1]; cz q[1],q[2]; swap q[0],q[2]; cp(-pi/4) q[2],q[0];
ccx q[0],q[1],q[2];
barrier q[0],q[1];
id q[2];
)",
     R"(OPENQASM 3.0;
include "stdgates.inc";
qubit[3] q;
x q[0]; y q[1]; z q[2]; h q[0]; s q[1]; sdg q[2]; t q[0]; tdg q[1]; sx q[2];
rx(pi/2) q[0]; ry(-0.25) q[1]; rz(2*pi/3) q[2]; p(1.5e-1) q[0];
U(0.1, 0.2, 0.3) q[1];
cx q[0], q[1]; cz q[1], q[2]; swap q[0], q[2]; cp(-pi/4) q[2], q[0];
ccx q[0], q[1], q[2];
barrier q[0], q[1];
id q[2];
)"},
    {"interleaved measurements keep their place",
     R"(OPENQASM 2.0;
include "qelib1.inc";
qreg q[3];
creg c[3];
h q[0];
measure q[0] -> c[0];
rx(0.5) q[1];
cx q[1],q[2];
measure q[1] -> c[1];
ry(0.25) q[2];
measure q[2] -> c[2];
)",
     R"(OPENQASM 3.0;
include "stdgates.inc";
qubit[3] q;
bit[3] c;
h q[0];
c[0] = measure q[0];
rx(0.5) q[1];
cx q[1], q[2];
measure q[1] -> c[1];
ry(0.25) q[2];
c[2] = measure q[2];
)"},
};

TEST(Qasm3EquivalenceTest, ProgramsInBothLanguagesGiveIdenticalCircuits) {
    for (const Pair& pair : kPairs) {
        expect_identical(qasm2::parse(pair.qasm2), parse(pair.qasm3), pair.name);
    }
}

}  // namespace
}  // namespace menos::qasm3
