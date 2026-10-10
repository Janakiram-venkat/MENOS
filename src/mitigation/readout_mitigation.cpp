#include "mitigation/readout_mitigation.h"

#include <algorithm>
#include <cmath>
#include <format>

#include "common/checked_math.h"
#include "common/error.h"
#include "ir/final_writes.h"

namespace menos::mitigation {
namespace {

// Number of dense vectors peak_bytes() accounts for (see its declaration).
constexpr std::uint64_t kPeakVectors = 5;

Matrix2 inverse(const ClbitEstimate& e) {
    // A = [[1 - e0, e1], [e0, 1 - e1]]: columns are the true value.
    const double det = 1.0 - e.e0 - e.e1;
    return {(1.0 - e.e1) / det, -e.e1 / det, -e.e0 / det, (1.0 - e.e0) / det};
}

Matrix2 multiply(const Matrix2& x, const Matrix2& y) {
    return {x[0] * y[0] + x[1] * y[2],
            x[0] * y[1] + x[1] * y[3],
            x[2] * y[0] + x[3] * y[2],
            x[2] * y[1] + x[3] * y[3]};
}

std::size_t index_of(const BitString& outcome, std::span<const ClbitId> clbits) {
    std::size_t index = 0;
    for (std::size_t k = 0; k < clbits.size(); ++k) {
        if (outcome.test(clbits[k])) {
            index |= std::size_t{1} << k;
        }
    }
    return index;
}

}  // namespace

std::vector<std::pair<QubitId, ClbitId>> measured_pairs(const Circuit& circuit) {
    std::vector<std::pair<QubitId, ClbitId>> pairs;
    for (const auto& [clbit, qubit] : ir::compute_final_writes(circuit)) {
        pairs.emplace_back(qubit, clbit);
    }
    return pairs;
}

// zero_counts and one_counts are distinguished by name at the one production
// call site (src/api/mitigation.cpp), as the batch results of the all-zero and
// all-one calibration circuits.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
std::vector<ClbitEstimate> estimate_assignment(
    const std::map<BitString, std::uint64_t>& zero_counts,
    const std::map<BitString, std::uint64_t>& one_counts,
    std::span<const ClbitId> clbits,
    std::uint64_t calibration_shots) {
    // NOLINTEND(bugprone-easily-swappable-parameters)
    common::ensure(calibration_shots > 0, "estimate_assignment needs calibration shots");
    std::vector<ClbitEstimate> estimates;
    estimates.reserve(clbits.size());
    const auto shots = static_cast<double>(calibration_shots);
    for (const ClbitId clbit : clbits) {
        std::uint64_t ones_from_zero = 0;
        for (const auto& [outcome, count] : zero_counts) {
            ones_from_zero += outcome.test(clbit) ? count : 0;
        }
        std::uint64_t zeros_from_one = 0;
        for (const auto& [outcome, count] : one_counts) {
            zeros_from_one += outcome.test(clbit) ? 0 : count;
        }
        const ClbitEstimate e{static_cast<double>(ones_from_zero) / shots,
                              static_cast<double>(zeros_from_one) / shots};
        if (1.0 - e.e0 - e.e1 <= 0.0) {
            throw common::UnsupportedFeature(std::format(
                "readout mitigation: clbit {} has estimated P(1|0) + P(0|1) = {} >= 1, so its "
                "assignment matrix has no meaningful inverse",
                clbit,
                e.e0 + e.e1));
        }
        estimates.push_back(e);
    }
    return estimates;
}

std::vector<double> empirical_distribution(const std::map<BitString, std::uint64_t>& counts,
                                           std::span<const ClbitId> clbits) {
    std::vector<double> distribution(common::checked_pow2(static_cast<unsigned>(clbits.size())),
                                     0.0);
    std::uint64_t total = 0;
    for (const auto& [outcome, count] : counts) {
        distribution[index_of(outcome, clbits)] += static_cast<double>(count);
        total += count;
    }
    common::ensure(total > 0, "empirical_distribution needs at least one shot");
    for (double& p : distribution) {
        p /= static_cast<double>(total);
    }
    return distribution;
}

void apply_factor(const Matrix2& factor, std::size_t bit, std::vector<double>& vector) {
    const std::size_t stride = std::size_t{1} << bit;
    common::ensure(2 * stride <= vector.size(), "apply_factor: bit out of range");
    for (std::size_t base = 0; base < vector.size(); base += 2 * stride) {
        for (std::size_t i = base; i < base + stride; ++i) {
            const double v0 = vector[i];
            const double v1 = vector[i + stride];
            vector[i] = factor[0] * v0 + factor[1] * v1;
            vector[i + stride] = factor[2] * v0 + factor[3] * v1;
        }
    }
}

void apply_tensored(std::span<const Matrix2> factors, std::vector<double>& vector) {
    common::ensure(vector.size() == (std::size_t{1} << factors.size()),
                   "apply_tensored: vector size must be 2^(number of factors)");
    for (std::size_t k = 0; k < factors.size(); ++k) {
        apply_factor(factors[k], k, vector);
    }
}

Mitigation mitigate(std::span<const double> distribution,
                    std::uint64_t shots,
                    std::span<const ClbitEstimate> estimates,
                    std::uint64_t calibration_shots) {
    const std::size_t m = estimates.size();
    common::ensure(distribution.size() == (std::size_t{1} << m),
                   "mitigate: distribution size must be 2^m");
    common::ensure(shots > 0 && calibration_shots > 0, "mitigate: shot counts must be positive");

    std::vector<Matrix2> inverses;
    std::vector<Matrix2> squared;
    inverses.reserve(m);
    squared.reserve(m);
    double gamma = 1.0;
    for (const ClbitEstimate& e : estimates) {
        const Matrix2 inv = inverse(e);
        inverses.push_back(inv);
        squared.push_back({inv[0] * inv[0], inv[1] * inv[1], inv[2] * inv[2], inv[3] * inv[3]});
        gamma *= std::max(std::abs(inv[0]) + std::abs(inv[2]), std::abs(inv[1]) + std::abs(inv[3]));
    }

    Mitigation out;
    out.gamma = gamma;
    out.quasi_probabilities.assign(distribution.begin(), distribution.end());
    apply_tensored(inverses, out.quasi_probabilities);

    // Target shot noise: Cov(p) = (diag p - p p^T) / N and q = M p, so
    // Var(q_i) = [(M o M) p]_i / N - q_i^2 / N, and M o M is again a tensor
    // product (of the entrywise-squared factors).
    out.target_variance.assign(distribution.begin(), distribution.end());
    apply_tensored(squared, out.target_variance);
    const auto n = static_cast<double>(shots);
    for (std::size_t i = 0; i < out.target_variance.size(); ++i) {
        const double q = out.quasi_probabilities[i];
        out.target_variance[i] = (out.target_variance[i] - q * q) / n;
    }

    // Calibration noise, first order. q = A^-1 p with A = A_{m-1} (x) ... (x) A_0,
    // so dq/de = -A^-1 (dA/de) q. A parameter e of clbit c enters only factor c,
    // so dA/de = A_{m-1} (x) ... (x) dA_c/de (x) ... (x) A_0, and in A^-1 dA/de
    // every other factor cancels to the identity: dq/de = -(A_c^-1 dA_c/de)
    // acting on bit c of q alone. dA_c/de0 = [[-1, 0], [1, 0]] and
    // dA_c/de1 = [[0, 1], [0, -1]]. Each estimate is an independent binomial
    // proportion, Var(e) = e (1 - e) / K. The sign drops out of the square.
    constexpr Matrix2 kDe0{-1.0, 0.0, 1.0, 0.0};
    constexpr Matrix2 kDe1{0.0, 1.0, 0.0, -1.0};
    const auto k = static_cast<double>(calibration_shots);
    out.calibration_variance.assign(distribution.size(), 0.0);
    std::vector<double> derivative;
    for (std::size_t c = 0; c < m; ++c) {
        const std::array<std::pair<Matrix2, double>, 2> parameters{
            std::pair{kDe0, estimates[c].e0}, std::pair{kDe1, estimates[c].e1}};
        for (const auto& [d_a, e] : parameters) {
            const double var_e = e * (1.0 - e) / k;
            if (var_e == 0.0) {
                continue;
            }
            derivative = out.quasi_probabilities;
            apply_factor(multiply(inverses[c], d_a), c, derivative);
            for (std::size_t i = 0; i < derivative.size(); ++i) {
                out.calibration_variance[i] += derivative[i] * derivative[i] * var_e;
            }
        }
    }
    return out;
}

std::uint64_t peak_bytes(std::size_t measured_clbits) {
    const std::uint64_t entries =
        common::checked_pow2(static_cast<unsigned>(std::min<std::size_t>(measured_clbits, 63)));
    return common::checked_mul(common::checked_mul(kPeakVectors, entries), sizeof(double));
}

}  // namespace menos::mitigation
