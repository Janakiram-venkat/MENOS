#include "menos_benchmark_environment.h"

#if defined(__APPLE__)
#include <sys/sysctl.h>
#else
#include <fstream>
#include <sstream>
#endif

#include <sstream>

#include "plan/hardware_profile.h"

namespace menos_benchmark {
namespace {

std::string os_name() {
#if defined(__APPLE__)
    return "macOS";
#elif defined(__linux__)
    return "Linux";
#else
    return "unknown-os";
#endif
}

std::string arch_name() {
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__aarch64__)
    return "aarch64";
#else
    return "unknown-arch";
#endif
}

std::string compiler_and_build_type() {
    std::ostringstream oss;
#if defined(__clang__)
    oss << "Clang " << __clang_major__ << "." << __clang_minor__ << "." << __clang_patchlevel__;
#elif defined(__GNUC__)
    oss << "GCC " << __GNUC__ << "." << __GNUC_MINOR__ << "." << __GNUC_PATCHLEVEL__;
#else
    oss << "unknown-compiler";
#endif
#if defined(NDEBUG)
    oss << ", Release (NDEBUG)";
#else
    oss << ", Debug";
#endif
    return oss.str();
}

// A portable-enough (within this repo's supported platforms) CPU
// identification: /proc/cpuinfo's "model name" on Linux,
// sysctlbyname("machdep.cpu.brand_string") on macOS. Best-effort -- falls
// back to a placeholder rather than failing the benchmark if unavailable
// (e.g. a non-x86 Linux /proc/cpuinfo layout).
std::string cpu_identification() {
#if defined(__APPLE__)
    char buffer[256] = {};
    std::size_t size = sizeof(buffer);
    if (sysctlbyname("machdep.cpu.brand_string", buffer, &size, nullptr, 0) == 0) {
        return std::string(buffer);
    }
    return "unknown-cpu";
#elif defined(__linux__)
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line;
    while (std::getline(cpuinfo, line)) {
        constexpr std::string_view kPrefix = "model name";
        if (line.compare(0, kPrefix.size(), kPrefix) == 0) {
            const auto colon = line.find(':');
            if (colon != std::string::npos) {
                return line.substr(colon + 2);
            }
        }
    }
    return "unknown-cpu";
#else
    return "unknown-cpu";
#endif
}

}  // namespace

std::string describe_environment() {
    std::ostringstream oss;
    oss << "environment: os=" << os_name() << " arch=" << arch_name()
        << " compiler=" << compiler_and_build_type() << " cpu=" << cpu_identification();
    return oss.str();
}

std::uint64_t safe_allocation_budget_bytes() {
    const menos::plan::HardwareProfile profile = menos::plan::detect_hardware_profile();
    return static_cast<std::uint64_t>(static_cast<double>(profile.total_memory_bytes) *
                                      menos::plan::kDefaultMemoryBudgetFraction);
}

}  // namespace menos_benchmark
