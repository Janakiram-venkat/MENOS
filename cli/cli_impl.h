#pragma once

#include <ostream>
#include <string>
#include <vector>

namespace menos::cli {

// Parses argv[1:] (command, file, flags), runs it, and writes exactly
// what the real `menos` binary would print to out/err. Returns the
// process exit code: 0 on success, 1 on any usage or menos::common::Error
// failure (docs/BUILD_PLAN.md Phase 7). Exposed as a function taking
// explicit streams (rather than hardcoding std::cout/std::cerr) so
// tests/cli/golden_test.cpp can exercise the exact same command-
// dispatch and formatting logic the built menos binary runs, in
// process, without spawning a subprocess.
[[nodiscard]] int run(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);

}  // namespace menos::cli
