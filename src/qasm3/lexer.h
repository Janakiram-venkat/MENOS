#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace menos::qasm3::detail {

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
    LBrace,
    RBrace,
    Arrow,   // ->
    Equals,  // =
    Plus,
    Minus,
    Star,
    Slash,
    Power,  // **
    At,     // @
    Hash,   // #
    Other,  // any other character or operator; the parser refuses it in context
    Eof,
};

// line and column are 1-based; column counts Unicode code points.
struct Token {
    TokenKind kind;
    std::string text;
    double number = 0.0;
    int line = 0;
    int column = 0;
};

// Splits OpenQASM 3 source into tokens, dropping whitespace and `//` and
// `/* */` comments. Identifiers may contain non-ASCII UTF-8 characters, so
// `π`, `τ` and `ℯ` are identifiers. Throws common::InvalidCircuit, with the
// location, for an unterminated comment or string. A number followed by
// identifier characters (`100ns`, `0x1f`) is one Other token.
[[nodiscard]] std::vector<Token> tokenize(std::string_view text);

}  // namespace menos::qasm3::detail
