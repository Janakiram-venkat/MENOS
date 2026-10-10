#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

namespace menos::common {

// Base of every exception MENOS throws. Derives std::runtime_error so
// existing catch(const std::exception&) handlers keep working.
class Error : public std::runtime_error {
public:
    explicit Error(std::string_view message);
};

// The input violates KIR rules.
class InvalidCircuit : public Error {
public:
    explicit InvalidCircuit(std::string_view message);
};

// Valid input that the current phase does not support.
class UnsupportedFeature : public Error {
public:
    explicit UnsupportedFeature(std::string_view message);
};

// A memory budget or size limit was exceeded.
class ResourceExceeded : public Error {
public:
    explicit ResourceExceeded(std::string_view message);
};

// A violated internal invariant. Raised only through ensure() below.
class InternalError : public Error {
public:
    explicit InternalError(std::string_view message);
};

// Throws InternalError(message) if condition is false. The single choke
// point for internal invariant checks (CLAUDE.md: "raised through a single
// ensure(condition, message) helper").
void ensure(bool condition, std::string_view message);

}  // namespace menos::common
