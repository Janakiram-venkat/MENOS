#include "menos/qasm2.h"

#include <cctype>
#include <cstdint>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "common/error.h"
#include "ir/gate_table.h"

namespace menos::qasm2 {
namespace {

using common::InvalidCircuit;
using common::UnsupportedFeature;

enum class TokenKind : std::uint8_t {
    Ident,
    Int,
    Real,
    String,
    Semicolon,
    Comma,
    LParen,
    RParen,
    LBracket,
    RBracket,
    Arrow,
    Plus,
    Minus,
    Star,
    Slash,
    // Any punctuation this grammar never expects (braces, '=', a lone
    // '>' outside '->', '!', ...): never a lex error, only ever an
    // unexpected token if the parser reaches it while trying to parse a
    // statement this grammar does support.
    Other,
    Eof,
};

struct Token {
    TokenKind kind;
    std::string text;   // Ident/String content
    double number = 0;  // Int/Real value
    int line = 0;
};

// Tokenizes the whole input up front (small files, no reason for a
// re-entrant lexer). `//` runs to end of line; OpenQASM 2 has no block
// comments.
std::vector<Token> tokenize(std::string_view text) {
    std::vector<Token> tokens;
    std::size_t i = 0;
    int line = 1;
    const std::size_t n = text.size();

    auto is_ident_start = [](char c) {
        return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
    };
    auto is_ident_char = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    };

    while (i < n) {
        const char c = text[i];
        if (c == '\n') {
            ++line;
            ++i;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
            continue;
        }
        if (c == '/' && i + 1 < n && text[i + 1] == '/') {
            while (i < n && text[i] != '\n') {
                ++i;
            }
            continue;
        }
        if (c == '"') {
            const std::size_t start_line = static_cast<std::size_t>(line);
            ++i;
            std::string value;
            while (i < n && text[i] != '"') {
                if (text[i] == '\n') {
                    throw InvalidCircuit("qasm2: unterminated string literal at line " +
                                         std::to_string(start_line));
                }
                value += text[i];
                ++i;
            }
            if (i >= n) {
                throw InvalidCircuit("qasm2: unterminated string literal at line " +
                                     std::to_string(start_line));
            }
            ++i;  // closing quote
            tokens.push_back(Token{
                .kind = TokenKind::String, .text = value, .line = static_cast<int>(start_line)});
            continue;
        }
        if (is_ident_start(c)) {
            std::size_t start = i;
            while (i < n && is_ident_char(text[i])) {
                ++i;
            }
            tokens.push_back(Token{.kind = TokenKind::Ident,
                                   .text = std::string(text.substr(start, i - start)),
                                   .line = line});
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            std::size_t start = i;
            bool is_real = false;
            while (i < n && std::isdigit(static_cast<unsigned char>(text[i]))) {
                ++i;
            }
            if (i < n && text[i] == '.') {
                is_real = true;
                ++i;
                while (i < n && std::isdigit(static_cast<unsigned char>(text[i]))) {
                    ++i;
                }
            }
            if (i < n && (text[i] == 'e' || text[i] == 'E')) {
                std::size_t exp_start = i;
                ++i;
                if (i < n && (text[i] == '+' || text[i] == '-')) {
                    ++i;
                }
                if (i < n && std::isdigit(static_cast<unsigned char>(text[i]))) {
                    is_real = true;
                    while (i < n && std::isdigit(static_cast<unsigned char>(text[i]))) {
                        ++i;
                    }
                } else {
                    // Not actually an exponent (e.g. a bare "2e" gate
                    // name never occurs here since idents start with a
                    // letter) -- back off.
                    i = exp_start;
                }
            }
            const std::string literal(text.substr(start, i - start));
            tokens.push_back(Token{.kind = is_real ? TokenKind::Real : TokenKind::Int,
                                   .text = literal,
                                   .number = std::stod(literal),
                                   .line = line});
            continue;
        }
        TokenKind single_char_kind = TokenKind::Other;
        switch (c) {
            case ';':
                single_char_kind = TokenKind::Semicolon;
                break;
            case ',':
                single_char_kind = TokenKind::Comma;
                break;
            case '(':
                single_char_kind = TokenKind::LParen;
                break;
            case ')':
                single_char_kind = TokenKind::RParen;
                break;
            case '[':
                single_char_kind = TokenKind::LBracket;
                break;
            case ']':
                single_char_kind = TokenKind::RBracket;
                break;
            case '+':
                single_char_kind = TokenKind::Plus;
                break;
            case '*':
                single_char_kind = TokenKind::Star;
                break;
            case '/':
                single_char_kind = TokenKind::Slash;
                break;
            case '-':
                single_char_kind = TokenKind::Minus;
                break;
            default:
                // Any other punctuation (braces, '=', '>' on its own,
                // '!', ...) becomes an opaque Other token rather than a
                // lex error: this parser rejects unsupported constructs
                // (gate blocks, if, opaque, reset) at the statement
                // level, by keyword, before ever trying to interpret
                // their bodies -- the lexer must not choke on syntax
                // that only ever appears inside a construct the parser
                // is about to reject anyway.
                single_char_kind = TokenKind::Other;
        }
        tokens.push_back(Token{.kind = single_char_kind, .text = "", .line = line});
        ++i;
        // '->' is two characters; the '-' above already recorded Minus,
        // so a following '>' with no space is folded into Arrow here.
        if (tokens.back().kind == TokenKind::Minus && i < n && text[i] == '>') {
            tokens.back().kind = TokenKind::Arrow;
            ++i;
        }
    }
    tokens.push_back(Token{.kind = TokenKind::Eof, .text = "", .line = line});
    return tokens;
}

class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    Circuit run() {
        expect_openqasm_header();
        expect_include();
        parse_body();
        if (!num_qubits_.has_value()) {
            throw InvalidCircuit("qasm2: no qreg declaration found");
        }
        // Close the CSR-style offset arrays: qubit_offsets_/param_offsets_
        // so far hold each gate's own start offset only; append_batch's
        // contract needs one trailing entry equal to the total size too
        // (Circuit::append_batch's precondition, include/menos/circuit.h).
        qubit_offsets_.push_back(static_cast<std::uint32_t>(gate_qubits_.size()));
        param_offsets_.push_back(static_cast<std::uint32_t>(params_.size()));
        Circuit circuit(*num_qubits_, num_clbits_);
        circuit.append_batch(std::move(opcodes_),
                             std::move(gate_qubits_),
                             std::move(qubit_offsets_),
                             std::move(params_),
                             std::move(param_offsets_));
        for (const auto& [qubit, clbit] : measures_) {
            circuit.append_measure(qubit, clbit);
        }
        return circuit;
    }

private:
    const Token& peek() const {
        return tokens_[pos_];
    }
    const Token& advance() {
        return tokens_[pos_++];
    }

    [[noreturn]] void fail_malformed(const std::string& what) {
        throw InvalidCircuit("qasm2: " + what + " at line " + std::to_string(peek().line));
    }

    [[noreturn]] void fail_unsupported(const std::string& what) {
        throw UnsupportedFeature("qasm2: " + what + " at line " + std::to_string(peek().line));
    }

    const Token& expect(TokenKind kind, const std::string& what) {
        if (peek().kind != kind) {
            fail_malformed("expected " + what);
        }
        return advance();
    }

    void expect_ident(const std::string& text) {
        if (peek().kind != TokenKind::Ident || peek().text != text) {
            fail_malformed("expected '" + text + "'");
        }
        advance();
    }

    bool at_ident(const std::string& text) const {
        return peek().kind == TokenKind::Ident && peek().text == text;
    }

    void expect_openqasm_header() {
        expect_ident("OPENQASM");
        const Token& version = peek();
        if (version.kind != TokenKind::Real && version.kind != TokenKind::Int) {
            fail_malformed("expected a version number after OPENQASM");
        }
        advance();
        if (version.number != 2.0) {
            throw UnsupportedFeature("qasm2: unsupported OPENQASM version " + version.text +
                                     " at line " + std::to_string(version.line) +
                                     " -- only 2.0 is supported");
        }
        expect(TokenKind::Semicolon, "';' after the OPENQASM version");
    }

    void expect_include() {
        if (!at_ident("include")) {
            fail_unsupported("missing 'include \"qelib1.inc\";'");
        }
        advance();
        const Token& path = expect(TokenKind::String, "a quoted include path");
        if (path.text != "qelib1.inc") {
            fail_unsupported("unsupported include \"" + path.text +
                             "\" -- only \"qelib1.inc\" is supported");
        }
        expect(TokenKind::Semicolon, "';' after the include path");
    }

    QubitId parse_qubit_ref() {
        const Token& reg = expect(TokenKind::Ident, "a register name");
        expect(TokenKind::LBracket, "'[' after a register name");
        const Token& index_tok = expect(TokenKind::Int, "a non-negative integer index");
        expect(TokenKind::RBracket, "']' after a register index");
        const auto index = static_cast<QubitId>(index_tok.number);

        // reg.text == qreg_name_ can only be true once qreg_name_ (and
        // num_qubits_, set together in parse_qreg_decl()) is engaged --
        // std::optional::operator== already returns false for a
        // disengaged qreg_name_ against any string, so this path is
        // never reached before num_qubits_ is set. Not provable to
        // clang-tidy's local static analysis (mirrors the identical
        // pattern in src/plan/planner.cpp).
        // NOLINTBEGIN(bugprone-unchecked-optional-access)
        if (reg.text == qreg_name_) {
            if (index >= *num_qubits_) {
                fail_malformed("qubit index " + std::to_string(index) +
                               " out of range for register '" + reg.text + "' of size " +
                               std::to_string(*num_qubits_));
            }
            return index;
        }
        // NOLINTEND(bugprone-unchecked-optional-access)
        fail_malformed("reference to undeclared quantum register '" + reg.text + "'");
    }

    ClbitId parse_clbit_ref() {
        const Token& reg = expect(TokenKind::Ident, "a register name");
        expect(TokenKind::LBracket, "'[' after a register name");
        const Token& index_tok = expect(TokenKind::Int, "a non-negative integer index");
        expect(TokenKind::RBracket, "']' after a register index");
        const auto index = static_cast<ClbitId>(index_tok.number);

        if (!creg_name_.has_value() || reg.text != *creg_name_) {
            fail_malformed("reference to undeclared classical register '" + reg.text + "'");
        }
        if (index >= num_clbits_) {
            fail_malformed("classical bit index " + std::to_string(index) +
                           " out of range for register '" + reg.text + "' of size " +
                           std::to_string(num_clbits_));
        }
        return index;
    }

    // expr := term (('+' | '-') term)*
    double parse_expr() {
        double value = parse_term();
        while (peek().kind == TokenKind::Plus || peek().kind == TokenKind::Minus) {
            const bool is_plus = peek().kind == TokenKind::Plus;
            advance();
            const double rhs = parse_term();
            value = is_plus ? value + rhs : value - rhs;
        }
        return value;
    }

    // term := unary (('*' | '/') unary)*
    double parse_term() {
        double value = parse_unary();
        while (peek().kind == TokenKind::Star || peek().kind == TokenKind::Slash) {
            const bool is_mul = peek().kind == TokenKind::Star;
            advance();
            const double rhs = parse_unary();
            value = is_mul ? value * rhs : value / rhs;
        }
        return value;
    }

    // unary := ('+' | '-')? primary
    double parse_unary() {
        if (peek().kind == TokenKind::Plus) {
            advance();
            return parse_unary();
        }
        if (peek().kind == TokenKind::Minus) {
            advance();
            return -parse_unary();
        }
        return parse_primary();
    }

    // primary := Real | Int | 'pi' | '(' expr ')'
    double parse_primary() {
        if (peek().kind == TokenKind::Real || peek().kind == TokenKind::Int) {
            return advance().number;
        }
        if (at_ident("pi")) {
            advance();
            return std::numbers::pi_v<double>;
        }
        if (peek().kind == TokenKind::LParen) {
            advance();
            const double value = parse_expr();
            expect(TokenKind::RParen, "')'");
            return value;
        }
        fail_malformed("expected a number, 'pi' or '(' in a gate parameter expression");
    }

    // Consumes tokens up to and including the statement's terminating
    // ';' without interpreting them -- for barrier/id, both true no-ops
    // (docs/adr precedent: the existing Qiskit adapter's own
    // DROPPED_INSTRUCTION_NAMES already drops these two identically).
    void skip_to_semicolon() {
        while (peek().kind != TokenKind::Semicolon) {
            if (peek().kind == TokenKind::Eof) {
                fail_malformed("unterminated statement (missing ';')");
            }
            advance();
        }
        advance();
    }

    void parse_qreg_decl() {
        advance();  // 'qreg'
        if (num_qubits_.has_value()) {
            fail_unsupported("more than one quantum register is not supported");
        }
        const Token& name = expect(TokenKind::Ident, "a register name after 'qreg'");
        expect(TokenKind::LBracket, "'[' after a qreg name");
        const Token& size = expect(TokenKind::Int, "a positive integer register size");
        expect(TokenKind::RBracket, "']' after a qreg size");
        expect(TokenKind::Semicolon, "';' after a qreg declaration");
        if (size.number <= 0) {
            fail_malformed("qreg size must be positive");
        }
        qreg_name_ = name.text;
        num_qubits_ = static_cast<std::uint32_t>(size.number);
    }

    void parse_creg_decl() {
        advance();  // 'creg'
        if (creg_name_.has_value()) {
            fail_unsupported("more than one classical register is not supported");
        }
        const Token& name = expect(TokenKind::Ident, "a register name after 'creg'");
        expect(TokenKind::LBracket, "'[' after a creg name");
        const Token& size = expect(TokenKind::Int, "a positive integer register size");
        expect(TokenKind::RBracket, "']' after a creg size");
        expect(TokenKind::Semicolon, "';' after a creg declaration");
        if (size.number <= 0) {
            fail_malformed("creg size must be positive");
        }
        creg_name_ = name.text;
        num_clbits_ = static_cast<std::uint32_t>(size.number);
    }

    void parse_measure_stmt() {
        advance();  // 'measure'
        const QubitId qubit = parse_qubit_ref();
        expect(TokenKind::Arrow, "'->' in a measure statement");
        const ClbitId clbit = parse_clbit_ref();
        expect(TokenKind::Semicolon, "';' after a measure statement");
        measures_.emplace_back(qubit, clbit);
    }

    void parse_gate_call(const std::string& name, int line) {
        advance();  // the gate name

        std::vector<double> params;
        if (peek().kind == TokenKind::LParen) {
            advance();
            if (peek().kind != TokenKind::RParen) {
                params.push_back(parse_expr());
                while (peek().kind == TokenKind::Comma) {
                    advance();
                    params.push_back(parse_expr());
                }
            }
            expect(TokenKind::RParen, "')' after gate parameters");
        }

        std::vector<QubitId> qubits;
        qubits.push_back(parse_qubit_ref());
        while (peek().kind == TokenKind::Comma) {
            advance();
            qubits.push_back(parse_qubit_ref());
        }
        expect(TokenKind::Semicolon, "';' after a gate statement");

        const ir::GateInfo* info = ir::gate_info_by_name(name);
        if (info == nullptr) {
            throw UnsupportedFeature("qasm2: unsupported gate '" + name + "' at line " +
                                     std::to_string(line));
        }
        if (static_cast<int>(qubits.size()) != info->arity) {
            fail_malformed("gate '" + name + "' takes " + std::to_string(info->arity) +
                           " qubit(s), got " + std::to_string(qubits.size()));
        }
        if (static_cast<int>(params.size()) != info->num_params) {
            fail_malformed("gate '" + name + "' takes " + std::to_string(info->num_params) +
                           " parameter(s), got " + std::to_string(params.size()));
        }

        opcodes_.push_back(info->opcode);
        qubit_offsets_.push_back(static_cast<std::uint32_t>(gate_qubits_.size()));
        param_offsets_.push_back(static_cast<std::uint32_t>(params_.size()));
        for (const QubitId q : qubits) {
            gate_qubits_.push_back(q);
        }
        for (const double p : params) {
            params_.push_back(p);
        }
    }

    void parse_body() {
        while (peek().kind != TokenKind::Eof) {
            if (at_ident("qreg")) {
                parse_qreg_decl();
            } else if (at_ident("creg")) {
                parse_creg_decl();
            } else if (at_ident("measure")) {
                if (!num_qubits_.has_value()) {
                    fail_malformed("measure statement before any qreg declaration");
                }
                parse_measure_stmt();
            } else if (at_ident("barrier") || at_ident("id")) {
                advance();
                skip_to_semicolon();
            } else if (at_ident("gate")) {
                fail_unsupported("user-defined gate blocks beyond qelib1.inc are not supported");
            } else if (at_ident("if")) {
                fail_unsupported("classical control flow ('if') is not supported");
            } else if (at_ident("opaque")) {
                fail_unsupported("opaque gate declarations are not supported");
            } else if (at_ident("reset")) {
                fail_unsupported("'reset' is not supported");
            } else if (at_ident("include")) {
                fail_unsupported("only a single 'include \"qelib1.inc\";' is supported");
            } else if (peek().kind == TokenKind::Ident) {
                if (!num_qubits_.has_value()) {
                    fail_malformed("gate statement before any qreg declaration");
                }
                const std::string name = peek().text;
                const int line = peek().line;
                parse_gate_call(name, line);
            } else {
                fail_malformed("expected a statement");
            }
        }
    }

    std::vector<Token> tokens_;
    std::size_t pos_ = 0;

    std::optional<std::string> qreg_name_;
    std::optional<std::uint32_t> num_qubits_;
    std::optional<std::string> creg_name_;
    std::uint32_t num_clbits_ = 0;

    std::vector<Opcode> opcodes_;
    std::vector<QubitId> gate_qubits_;
    std::vector<std::uint32_t> qubit_offsets_;
    std::vector<double> params_;
    std::vector<std::uint32_t> param_offsets_;
    std::vector<std::pair<QubitId, ClbitId>> measures_;
};

}  // namespace

Circuit parse(std::string_view text) {
    Parser parser(tokenize(text));
    return parser.run();
}

}  // namespace menos::qasm2
