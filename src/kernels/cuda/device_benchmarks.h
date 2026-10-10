#pragma once

#include <chrono>
#include <cstdint>

namespace menos::kernels::cuda {

// Device microbenchmarks that need nothing above the kernels layer
// (docs/adr/0014). They live here rather than in plan/ so that the CUDA
// runtime header stays confined to this directory: everything above calls
// these through menos types and never includes <cuda_runtime.h> itself.
//
// The remaining calibration figure, marginal device throughput, cannot move
// here: it measures a real circuit through CudaDenseEngine, which is in the
// engines layer above this one. It lives in plan/cuda_calibration.cpp and uses
// the same timing policy below.

// Repeats `body` until both the deadline and the minimum repetition count are
// met, and returns seconds per repetition.
//
// The whole loop runs twice and the first result is discarded. That is not
// decoration: the first pass after an allocation pays page faults, cold TLB
// entries and clock ramp, none of which are the quantity being measured, and
// on a Release build a single un-warmed pass was observed to report roughly
// six times the true rate's inverse. The minimum repetition count covers the
// opposite case, where one repetition already exceeds the deadline and the
// measurement would otherwise rest on a single sample.
inline constexpr double kBenchmarkDeadlineSeconds = 0.015;
inline constexpr std::uint64_t kBenchmarkMinRepetitions = 2;

template <typename Body>
[[nodiscard]] double seconds_per_repetition(Body&& body) {
    using Clock = std::chrono::steady_clock;
    const auto timed_pass = [&]() {
        std::uint64_t repetitions = 0;
        const Clock::time_point start = Clock::now();
        double seconds = 0.0;
        do {
            body();
            ++repetitions;
            seconds = std::chrono::duration<double>(Clock::now() - start).count();
        } while (seconds < kBenchmarkDeadlineSeconds || repetitions < kBenchmarkMinRepetitions);
        return seconds / static_cast<double>(repetitions);
    };
    static_cast<void>(timed_pass());  // discarded
    return timed_pass();
}

// PCIe transfer rates, measured with pageable host memory because that is what
// the backend actually transfers (a std::vector). Pinned memory would flatter
// the figure by testing a path the engine does not take.
struct TransferRates {
    double host_to_device_bytes_per_second = 0.0;
    double device_to_host_bytes_per_second = 0.0;
};

// Both zero if no device is available.
[[nodiscard]] TransferRates measure_transfer_rates();

// The fixed cost of one kernel launch as the backend actually pays it: the
// constant-memory upload, the launch and the synchronize. Measured on a state
// small enough that a kernel's own work is negligible beside launching it.
//
// This is the term that makes a small circuit slower on the GPU than on the
// CPU, so a cost model without it would claim the device always wins. Zero if
// no device is available.
[[nodiscard]] double measure_kernel_launch_seconds();

}  // namespace menos::kernels::cuda
