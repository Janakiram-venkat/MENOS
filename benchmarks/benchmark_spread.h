#pragma once

// The run-to-run spread of a benchmark's timed repeats (docs/adr/0019
// section 2, docs/adr/0020 section 2): median, quartiles and range, computed
// exactly as tools/dashboard/menos_dashboard.py's spread() computes them
// (Python's statistics.quantiles(..., method="inclusive")), so a CUDA table
// and the dashboard report the same statistic for the same samples.

#include <algorithm>
#include <cstddef>
#include <vector>

namespace menos::benchmarks {

struct Spread {
    double median;
    double min;
    double max;
    double q1;
    double q3;
};

// The inclusive quantile at fraction p of sorted (non-empty) samples: linear
// interpolation at position p * (n - 1).
inline double inclusive_quantile(const std::vector<double>& sorted, double p) {
    const double position = p * static_cast<double>(sorted.size() - 1);
    const auto lower = static_cast<std::size_t>(position);
    const std::size_t upper = std::min(lower + 1, sorted.size() - 1);
    const double fraction = position - static_cast<double>(lower);
    return sorted[lower] + (fraction * (sorted[upper] - sorted[lower]));
}

// samples must be non-empty. With a single sample every statistic is that
// sample.
inline Spread spread(std::vector<double> samples) {
    std::sort(samples.begin(), samples.end());
    return Spread{
        .median = inclusive_quantile(samples, 0.5),
        .min = samples.front(),
        .max = samples.back(),
        .q1 = inclusive_quantile(samples, 0.25),
        .q3 = inclusive_quantile(samples, 0.75),
    };
}

}  // namespace menos::benchmarks
