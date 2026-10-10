#pragma once

#include <cstdint>
#include <limits>
#include <string>

#include "common/error.h"

namespace menos::common {

// Overflow-checked size arithmetic. Every function here throws
// ResourceExceeded rather than overflowing silently or invoking undefined
// behavior. CLAUDE.md: "Compute 2^n only through the checked helpers.
// Never shift by n >= 63... failure throws ResourceExceeded, never
// std::bad_alloc." checked_add and checked_mul throw the same exception
// type as checked_pow2 for consistency: within this header, every overflow
// means the requested size cannot be represented, which is a size limit
// being exceeded.

[[nodiscard]] inline std::uint64_t checked_add(std::uint64_t a, std::uint64_t b) {
    if (a > std::numeric_limits<std::uint64_t>::max() - b) {
        throw ResourceExceeded("checked_add: " + std::to_string(a) + " + " + std::to_string(b) +
                               " overflows uint64_t");
    }
    return a + b;
}

[[nodiscard]] inline std::uint64_t checked_mul(std::uint64_t a, std::uint64_t b) {
    if (a != 0 && b > std::numeric_limits<std::uint64_t>::max() / a) {
        throw ResourceExceeded("checked_mul: " + std::to_string(a) + " * " + std::to_string(b) +
                               " overflows uint64_t");
    }
    return a * b;
}

// Returns 2^n as a uint64_t for n <= 62. Throws ResourceExceeded for
// n >= 63, so a shift by 63 or more (undefined for a 64-bit unsigned type
// only when n >= 64, but reserved starting at 63 per CLAUDE.md's domain
// invariant "Never shift by n >= 63") never happens.
[[nodiscard]] inline std::uint64_t checked_pow2(unsigned n) {
    if (n > 62) {
        throw ResourceExceeded("checked_pow2: n = " + std::to_string(n) +
                               " exceeds the maximum of 62");
    }
    return std::uint64_t{1} << n;
}

}  // namespace menos::common
