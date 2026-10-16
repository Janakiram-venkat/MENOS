// Every construct outside the subset is refused with UnsupportedFeature,
// naming the construct, its line and its column (docs/adr/0023 section 2.2,
// gate criterion 2).

#include <gtest/gtest.h>

#include <string>

#include "common/error.h"
#include "menos/qasm3.h"

namespace menos::qasm3 {
namespace {

struct Refusal {
    const char* construct;
    const char* statement;  // placed on line 4, after a fixed 3-line preamble
    int column;
    const char* message;  // a substring the message must contain
};

constexpr const char* kPreamble =
    "OPENQASM 3.0;\ninclude \"stdgates.inc\";\nqubit[2] q; bit[2] c;\n";

const Refusal kRefusals[] = {
    {"reset", "reset q[0];", 1, "'reset'"},
    {"if", "if (c[0]) x q[0];", 1, "'if'"},
    {"else", "else x q[0];", 1, "'else'"},
    {"for", "for uint i in [0:1] x q[0];", 1, "'for'"},
    {"while", "while (true) x q[0];", 1, "'while'"},
    {"switch", "switch (c) { }", 1, "'switch'"},
    {"break", "break;", 1, "'break'"},
    {"continue", "continue;", 1, "'continue'"},
    {"end", "end;", 1, "'end'"},
    {"return", "return;", 1, "'return'"},
    {"def", "def f(qubit a) { x a; }", 1, "'def'"},
    {"extern", "extern f(int) -> int;", 1, "'extern'"},
    {"delay", "delay[100ns] q[0];", 1, "'delay'"},
    {"box", "box { x q[0]; }", 1, "'box'"},
    {"duration", "duration d = 10ns;", 1, "'duration'"},
    {"stretch", "stretch s;", 1, "'stretch'"},
    {"defcal", "defcal x $0 { }", 1, "'defcal'"},
    {"defcalgrammar", "defcalgrammar \"openpulse\";", 1, "'defcalgrammar'"},
    {"cal", "cal { }", 1, "'cal'"},
    {"output", "output float r;", 1, "'output'"},
    {"let", "let a = q[0];", 1, "'let'"},
    {"opaque", "opaque g a;", 1, "'opaque'"},
    {"int variable", "int i = 3;", 1, "classical variables"},
    {"float variable", "float f;", 1, "classical variables"},
    {"bool variable", "bool b = true;", 1, "classical variables"},
    {"array", "array[int[8], 2] a;", 1, "arrays"},
    {"classical assignment", "c[0] = 1;", 1, "classical assignments"},
    {"bit initialisation", "bit[1] e = \"0\";", 10, "initialising"},
    {"measure without target", "measure q[0];", 1, "without a target"},
    {"measure as a value", "x q[0]; rz(measure) q[0];", 12, "measurement used as a value"},
    {"non-integer pow", "pow(0.5) @ x q[0];", 1, "non-integer exponent"},
    {"register broadcast", "x q;", 3, "whole register"},
    {"register slice", "measure q[0:1] -> c[0:1];", 12, "slices"},
    {"physical qubit", "x $0;", 3, "physical qubits"},
    {"pragma", "#pragma foo", 1, "pragmas"},
    {"annotation", "@bind x q[0];", 1, "annotations"},
    {"other include", "include \"other.inc\";", 1, "stdgates.inc"},
    {"input int", "input int n;", 7, "'int' declaration"},
    {"sized angle", "input angle[20] a;", 12, "angle[20]"},
    {"float[32]", "const float[32] f = 1.0;", 12, "float[32]"},
    {"integer division", "rz(1 / 2) q[0];", 6, "division of two integers"},
    {"unknown function", "rz(cosh(1.0)) q[0];", 4, "function 'cosh'"},
};

TEST(Qasm3RefusalTest, EachConstructOutsideTheSubsetIsRefusedByNameAndLocation) {
    for (const Refusal& r : kRefusals) {
        const std::string text = std::string(kPreamble) + r.statement + "\n";
        try {
            static_cast<void>(parse(text));
            ADD_FAILURE() << r.construct << " was accepted";
        } catch (const common::UnsupportedFeature& error) {
            const std::string what = error.what();
            EXPECT_NE(what.find(r.message), std::string::npos) << r.construct << ": " << what;
            const std::string where = "line 4, column " + std::to_string(r.column);
            EXPECT_NE(what.find(where), std::string::npos) << r.construct << ": " << what;
        } catch (const common::Error& error) {
            ADD_FAILURE() << r.construct << " threw a different error: " << error.what();
        }
    }
}

TEST(Qasm3RefusalTest, AnOpenQasmVersionOtherThanThreeIsRefused) {
    EXPECT_THROW(static_cast<void>(parse("OPENQASM 2.0;\nqubit q;")), common::UnsupportedFeature);
    EXPECT_THROW(static_cast<void>(parse("OPENQASM 3.1;\nqubit q;")), common::UnsupportedFeature);
}

TEST(Qasm3RefusalTest, RefusedKeywordsInsideAGateBodyAreRefusedToo) {
    EXPECT_THROW(static_cast<void>(parse("qubit q; gate g a { reset a; } g q;")),
                 common::UnsupportedFeature);
}

}  // namespace
}  // namespace menos::qasm3
