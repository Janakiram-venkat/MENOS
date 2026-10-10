#pragma once

// Statistical helpers for the Phase 14 readout-mitigation tests
// (docs/adr/0018 section 6). Copied verbatim from tests/api/
// trajectory_run_test.cpp, which keeps its own copy: Phase 14 gate criterion 7
// requires every pre-existing test file to pass unmodified.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace menos::test_statistics {

// z with P(Z > z) = upper_tail for a standard normal Z, by bisection on
// P(Z > z) = erfc(z / sqrt 2) / 2.
inline double normal_upper_quantile(double upper_tail) {
    double lo = 0.0;
    double hi = 40.0;
    for (int i = 0; i < 200; ++i) {
        const double mid = 0.5 * (lo + hi);
        if (0.5 * std::erfc(mid / std::sqrt(2.0)) > upper_tail) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return 0.5 * (lo + hi);
}

// The regularized lower incomplete gamma function P(a, x): the series for
// x < a + 1, the continued fraction otherwise (Numerical Recipes, gser/gcf).
inline double regularized_lower_gamma(double a, double x) {
    if (x <= 0.0) {
        return 0.0;
    }
    const double log_prefix = -x + a * std::log(x) - std::lgamma(a);
    if (x < a + 1.0) {
        double term = 1.0 / a;
        double sum = term;
        for (int n = 1; n < 10000; ++n) {
            term *= x / (a + n);
            sum += term;
            if (std::abs(term) < std::abs(sum) * 1e-16) {
                break;
            }
        }
        return sum * std::exp(log_prefix);
    }
    constexpr double kTiny = 1e-300;
    double b = x + 1.0 - a;
    double c = 1.0 / kTiny;
    double d = 1.0 / b;
    double h = d;
    for (int i = 1; i < 10000; ++i) {
        const double an = -i * (i - a);
        b += 2.0;
        d = an * d + b;
        d = std::abs(d) < kTiny ? kTiny : d;
        c = b + an / c;
        c = std::abs(c) < kTiny ? kTiny : c;
        d = 1.0 / d;
        const double delta = d * c;
        h *= delta;
        if (std::abs(delta - 1.0) < 1e-16) {
            break;
        }
    }
    return 1.0 - std::exp(log_prefix) * h;
}

// x with P(chi^2_k <= x) = p.
inline double chi_square_quantile(double k, double p) {
    double lo = 0.0;
    double hi = 20.0 * k;
    for (int i = 0; i < 300; ++i) {
        const double mid = 0.5 * (lo + hi);
        if (regularized_lower_gamma(0.5 * k, 0.5 * mid) < p) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return 0.5 * (lo + hi);
}

// The exact two-sided binomial p-value of observing k successes in n trials
// with success probability p: the total probability of every outcome no more
// likely than k. k, n and p have different roles and types at every call site.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
inline double binomial_two_sided_p(std::uint64_t k, std::uint64_t n, double p) {
    const auto log_pmf = [&](std::uint64_t i) {
        const auto di = static_cast<double>(i);
        const auto dn = static_cast<double>(n);
        return std::lgamma(dn + 1.0) - std::lgamma(di + 1.0) - std::lgamma(dn - di + 1.0) +
               di * std::log(p) + (dn - di) * std::log1p(-p);
    };
    const double observed = log_pmf(k);
    double total = 0.0;
    for (std::uint64_t i = 0; i <= n; ++i) {
        const double l = log_pmf(i);
        if (l <= observed + 1e-9) {
            total += std::exp(l);
        }
    }
    return std::min(1.0, total);
}

}  // namespace menos::test_statistics
