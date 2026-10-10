#pragma once

#include <algorithm>
#include <cstdint>
#include <thread>

namespace menos::common {

// The default number of threads MENOS's CPU kernels may use: every hardware
// thread (owner decision, docs/adr/0019 section 3.3, Q3). hardware_concurrency()
// may report 0 when the count is unknown; that is read as 1, never guessed
// upward. OMP_NUM_THREADS is deliberately not consulted (section 3.3).
//
// Read once per process and cached: glibc's hardware_concurrency() opens and
// reads a /sys file on every call (measured at about 2 us warm under WSL, more
// when cold), which showed as a per-run cost on the smallest circuits.
[[nodiscard]] inline std::uint32_t default_thread_count() {
    static const std::uint32_t kCount = std::max(1U, std::thread::hardware_concurrency());
    return kCount;
}

}  // namespace menos::common
