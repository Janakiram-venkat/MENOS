#include "plan/hardware_profile.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace menos::plan {
namespace {

TEST(DetectHardwareProfileTest, ReturnsAPositiveMemorySize) {
    const HardwareProfile hw = detect_hardware_profile();
    EXPECT_GT(hw.total_memory_bytes, 0U);
}

#ifdef _WIN32
// The Windows probe (GlobalMemoryStatusEx) checked against an independent
// source: the RAM installed in the machine, as the firmware reports it. The
// memory the OS can use is a subset of what is installed, so it is positive
// and never larger (docs/adr/0022 section 2).
TEST(DetectHardwareProfileTest, WindowsTotalIsBoundedByInstalledMemory) {
    ULONGLONG installed_kib = 0;
    ASSERT_NE(GetPhysicallyInstalledSystemMemory(&installed_kib), 0);
    const std::uint64_t installed_bytes = static_cast<std::uint64_t>(installed_kib) * 1024U;
    const HardwareProfile hw = detect_hardware_profile();
    EXPECT_GT(hw.total_memory_bytes, 0U);
    EXPECT_LE(hw.total_memory_bytes, installed_bytes);
}
#endif

// detect_ is the uncalibrated path, and the planner distinguishes the two by
// exactly this: a zero in either figure means "never calibrated" and makes a
// time-budgeted job an error rather than a silent pass.
TEST(DetectHardwareProfileTest, LeavesBothCalibratedFiguresAtZero) {
    const HardwareProfile hw = detect_hardware_profile();
    EXPECT_EQ(hw.memory_bandwidth_bytes_per_second, 0.0);
    EXPECT_EQ(hw.work_units_per_second, 0.0);
}

TEST(CalibrateHardwareTest, ProducesPositiveBandwidthAndThroughput) {
    const HardwareProfile hw = calibrate_hardware();
    EXPECT_GT(hw.total_memory_bytes, 0U);
    EXPECT_GT(hw.memory_bandwidth_bytes_per_second, 0.0);
    EXPECT_GT(hw.work_units_per_second, 0.0);
}

// A regression test for a real defect, not a smoke test. With the volatile
// observer removed from measure_memory_bandwidth()'s loop, GCC at -O3 deleted
// every memcpy as a dead store and the warm-up pass reported 558691 copies in
// 15 ms -- 2.5 PB/s. Any figure above this ceiling means the copies are being
// elided again and the measurement is timing an empty loop.
//
// The bound is deliberately far above any real machine (1 TB/s; the fastest
// current server memory subsystems are an order of magnitude below it) so the
// test cannot fail on fast hardware. It bounds "did the work happen at all",
// which is the thing that actually broke.
TEST(CalibrateHardwareTest, BandwidthIsPhysicallyPlausible) {
    const HardwareProfile hw = calibrate_hardware();
    constexpr double kImplausibleBytesPerSecond = 1.0e12;
    EXPECT_LT(hw.memory_bandwidth_bytes_per_second, kImplausibleBytesPerSecond)
        << "measured " << hw.memory_bandwidth_bytes_per_second
        << " B/s, which exceeds any real memory subsystem -- the timed copies "
           "are almost certainly being optimized away";
}

// The figures are measurements, so they are not expected to be bit-identical
// between calls. They are expected to be stable enough to plan against: if
// repeated calibration on an idle machine disagreed by an order of magnitude,
// a time budget would admit a candidate on one run and reject it on the next.
//
// Bandwidth is a property of the hardware and is the tighter of the two.
// Observed spread across builds during development was ~12%; the 4x band here
// is loose enough to survive a loaded CI machine while still catching the
// failure mode that matters (a figure that is not reproducible at all).
TEST(CalibrateHardwareTest, RepeatedCalibrationAgreesWithinAnOrderOfMagnitude) {
    std::vector<double> bandwidths;
    std::vector<double> throughputs;
    for (int i = 0; i < 3; ++i) {
        const HardwareProfile hw = calibrate_hardware();
        bandwidths.push_back(hw.memory_bandwidth_bytes_per_second);
        throughputs.push_back(hw.work_units_per_second);
    }

    const auto spread = [](const std::vector<double>& values) {
        const auto [lo, hi] = std::minmax_element(values.begin(), values.end());
        return *hi / *lo;
    };
    EXPECT_LT(spread(bandwidths), 4.0) << "bandwidth is not reproducible";
    EXPECT_LT(spread(throughputs), 4.0) << "throughput is not reproducible";
}

// The cache is what makes calibration affordable: without it every job in a
// process would repeat the microbenchmarks.
TEST(CalibratedHardwareProfileTest, IsCachedSoRepeatedCallsReturnTheSameObject) {
    const HardwareProfile& first = calibrated_hardware_profile();
    const HardwareProfile& second = calibrated_hardware_profile();
    EXPECT_EQ(&first, &second);
    EXPECT_EQ(first.memory_bandwidth_bytes_per_second, second.memory_bandwidth_bytes_per_second);
    EXPECT_EQ(first.work_units_per_second, second.work_units_per_second);
}

TEST(CalibratedHardwareProfileTest, CarriesTheCalibratedFigures) {
    const HardwareProfile& hw = calibrated_hardware_profile();
    EXPECT_GT(hw.total_memory_bytes, 0U);
    EXPECT_GT(hw.memory_bandwidth_bytes_per_second, 0.0);
    EXPECT_GT(hw.work_units_per_second, 0.0);
}

}  // namespace
}  // namespace menos::plan
