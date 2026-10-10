#include "cost/trajectory_count.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <string>

#include "common/checked_math.h"
#include "common/error.h"

namespace menos::cost {
namespace {

bool is_identity_term(const PauliString& pauli) {
    return std::ranges::all_of(pauli.x_words, [](std::uint64_t w) { return w == 0; }) &&
           std::ranges::all_of(pauli.z_words, [](std::uint64_t w) { return w == 0; });
}

}  // namespace

TrajectoryCount trajectory_count(std::uint64_t shots,
                                 const Observable* observable,
                                 std::optional<double> precision) {
    common::ensure((observable != nullptr) == precision.has_value(),
                   "trajectory_count: precision must be set iff an expectation is requested");
    TrajectoryCount out{shots, std::nullopt};
    // The two are set together (checked above); testing both keeps that visible.
    if (observable == nullptr || !precision.has_value()) {
        return out;
    }
    double c = 0.0;
    for (const auto& [pauli, coefficient] : observable->terms) {
        if (!is_identity_term(pauli)) {
            c += std::abs(coefficient);
        }
    }
    out.coefficient_bound = c;
    const double eps = *precision;
    // 2^63: exactly representable as a double and as a std::uint64_t, and far
    // beyond any trajectory count that could finish.
    constexpr double kLargest = 9223372036854775808.0;
    const double raw = std::ceil((c * c) / (eps * eps));
    if (!(raw < kLargest)) {
        throw common::ResourceExceeded(
            "trajectory count ceil(C^2 / eps^2) with C = " + std::to_string(c) +
            " and eps = " + std::to_string(eps) + " is not representable");
    }
    auto n_eps = std::max<std::uint64_t>(2, static_cast<std::uint64_t>(raw));
    // Rounding guard: the guarantee is C / sqrt(N) <= eps.
    while (c / std::sqrt(static_cast<double>(n_eps)) > eps) {
        n_eps = common::checked_add(n_eps, 1);
    }
    out.trajectories = std::max(shots, n_eps);
    return out;
}

}  // namespace menos::cost
