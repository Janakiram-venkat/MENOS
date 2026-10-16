#include "qasm3/lexer.h"

#include <cctype>
#include <cstddef>
#include <stdexcept>
#include <string>

#include "common/error.h"

namespace menos::qasm3::detail {
namespace {

using common::InvalidCircuit;

bool is_ident_start(unsigned char c) {
    return std::isalpha(c) != 0 || c == '_' || c >= 0x80;
}

bool is_ident_char(unsigned char c) {
    return std::isalnum(c) != 0 || c == '_' || c >= 0x80;
}

class Lexer {
public:
    explicit Lexer(std::string_view text) : text_(text) {}

    std::vector<Token> run() {
        std::vector<Token> tokens;
        for (;;) {
            skip_space_and_comments();
            if (pos_ >= text_.size()) {
                tokens.push_back(Token{TokenKind::Eof, "", 0.0, line_, column_});
                return tokens;
            }
            tokens.push_back(next());
        }
    }

private:
    [[noreturn]] void fail(const std::string& what, int line, int column) const {
        throw InvalidCircuit("qasm3: " + what + " at line " + std::to_string(line) + ", column " +
                             std::to_string(column));
    }

    unsigned char at(std::size_t offset = 0) const {
        const std::size_t i = pos_ + offset;
        return i < text_.size() ? static_cast<unsigned char>(text_[i]) : '\0';
    }

    // Advances one byte, keeping line and column; a UTF-8 continuation byte
    // does not start a new column.
    void bump() {
        const unsigned char c = at();
        ++pos_;
        if (c == '\n') {
            ++line_;
            column_ = 1;
        } else if ((c & 0xC0) != 0x80) {
            ++column_;
        }
    }

    void skip_space_and_comments() {
        for (;;) {
            if (pos_ >= text_.size()) {
                return;
            }
            if (std::isspace(at()) != 0) {
                bump();
            } else if (at() == '/' && at(1) == '/') {
                while (pos_ < text_.size() && at() != '\n') {
                    bump();
                }
            } else if (at() == '/' && at(1) == '*') {
                const int line = line_;
                const int column = column_;
                bump();
                bump();
                while (at() != '*' || at(1) != '/') {
                    if (pos_ >= text_.size()) {
                        fail("unterminated '/*' comment", line, column);
                    }
                    bump();
                }
                bump();
                bump();
            } else {
                return;
            }
        }
    }

    Token next() {
        const int line = line_;
        const int column = column_;
        const unsigned char c = at();
        if (is_ident_start(c)) {
            const std::size_t start = pos_;
            while (pos_ < text_.size() && is_ident_char(at())) {
                bump();
            }
            return Token{TokenKind::Ident,
                         std::string(text_.substr(start, pos_ - start)),
                         0.0,
                         line,
                         column};
        }
        if (std::isdigit(c) != 0 || (c == '.' && std::isdigit(at(1)) != 0)) {
            return number(line, column);
        }
        if (c == '"') {
            bump();
            const std::size_t start = pos_;
            while (at() != '"') {
                if (pos_ >= text_.size() || at() == '\n') {
                    fail("unterminated string", line, column);
                }
                bump();
            }
            std::string value(text_.substr(start, pos_ - start));
            bump();
            return Token{TokenKind::String, std::move(value), 0.0, line, column};
        }
        if (c == '-' && at(1) == '>') {
            bump();
            bump();
            return Token{TokenKind::Arrow, "->", 0.0, line, column};
        }
        if (c == '*' && at(1) == '*') {
            bump();
            bump();
            return Token{TokenKind::Power, "**", 0.0, line, column};
        }
        // Two-character operators that only classical code uses; kept whole so
        // the parser refuses them by name instead of misreading their halves.
        static constexpr std::string_view kPairs[] = {
            "==", "!=", "<=", ">=", "&&", "||", "<<", ">>", "+=", "-=", "*=", "/=", "++", "--"};
        for (const std::string_view pair : kPairs) {
            if (text_.substr(pos_, 2) == pair) {
                bump();
                bump();
                return Token{TokenKind::Other, std::string(pair), 0.0, line, column};
            }
        }
        bump();
        const std::string text(1, static_cast<char>(c));
        switch (c) {
            case ';':
                return Token{TokenKind::Semicolon, text, 0.0, line, column};
            case ',':
                return Token{TokenKind::Comma, text, 0.0, line, column};
            case '(':
                return Token{TokenKind::LParen, text, 0.0, line, column};
            case ')':
                return Token{TokenKind::RParen, text, 0.0, line, column};
            case '[':
                return Token{TokenKind::LBracket, text, 0.0, line, column};
            case ']':
                return Token{TokenKind::RBracket, text, 0.0, line, column};
            case '{':
                return Token{TokenKind::LBrace, text, 0.0, line, column};
            case '}':
                return Token{TokenKind::RBrace, text, 0.0, line, column};
            case '=':
                return Token{TokenKind::Equals, text, 0.0, line, column};
            case '+':
                return Token{TokenKind::Plus, text, 0.0, line, column};
            case '-':
                return Token{TokenKind::Minus, text, 0.0, line, column};
            case '*':
                return Token{TokenKind::Star, text, 0.0, line, column};
            case '/':
                return Token{TokenKind::Slash, text, 0.0, line, column};
            case '@':
                return Token{TokenKind::At, text, 0.0, line, column};
            case '#':
                return Token{TokenKind::Hash, text, 0.0, line, column};
            default:
                return Token{TokenKind::Other, text, 0.0, line, column};
        }
    }

    // [0-9]+ ('.' [0-9]*)? ([eE] [+-]? [0-9]+)?  or  '.' [0-9]+ (exponent)?
    // An identifier character straight after a number (a unit such as `ns`,
    // a hexadecimal prefix) is malformed here.
    Token number(int line, int column) {
        const std::size_t start = pos_;
        bool real = false;
        while (std::isdigit(at()) != 0) {
            bump();
        }
        if (at() == '.') {
            real = true;
            bump();
            while (std::isdigit(at()) != 0) {
                bump();
            }
        }
        if (at() == 'e' || at() == 'E') {
            const std::size_t sign = (at(1) == '+' || at(1) == '-') ? 1 : 0;
            if (std::isdigit(at(1 + sign)) != 0) {
                real = true;
                bump();
                if (sign != 0) {
                    bump();
                }
                while (std::isdigit(at()) != 0) {
                    bump();
                }
            }
        }
        if (is_ident_char(at())) {
            // A number with a suffix: a duration such as `100ns`, or a
            // hexadecimal or binary literal. One Other token, so the parser
            // refuses it where it appears instead of reading it as a number.
            while (pos_ < text_.size() && is_ident_char(at())) {
                bump();
            }
            return Token{TokenKind::Other,
                         std::string(text_.substr(start, pos_ - start)),
                         0.0,
                         line,
                         column};
        }
        const std::string text(text_.substr(start, pos_ - start));
        double value = 0.0;
        try {
            value = std::stod(text);
        } catch (const std::out_of_range&) {
            fail("number '" + text + "' is out of range", line, column);
        }
        return Token{real ? TokenKind::Real : TokenKind::Int, text, value, line, column};
    }

    std::string_view text_;
    std::size_t pos_ = 0;
    int line_ = 1;
    int column_ = 1;
};

}  // namespace

std::vector<Token> tokenize(std::string_view text) {
    return Lexer(text).run();
}

}  // namespace menos::qasm3::detail
