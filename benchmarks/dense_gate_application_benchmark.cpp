// Benchmark group 1 (docs/BUILD_PLAN.md Phase 3, 3.9): generic dense gate
// application at 16, 18, 20, 22, 24, 26 qubits, skipping a data point the
// machine cannot safely allocate rather than letting the OS kill the
// process. Benchmarks are not correctness tests (CLAUDE.md) -- correctness
// of apply_matrix is established in tests/engines/dense/apply_matrix_test.cpp.
//
// Phase 15 step 1 (docs/adr/0019 section 2) adds the baseline the dense CPU
// performance work is measured against: every benchmark runs kRepetitions
// times so the run-to-run spread is reported beside the median, a 1-qubit
// case sits beside the 2-qubit one (step 3 adds a separate 1-qubit kernel),
// and the two reductions that stay single-threaded -- one Pauli-term
// expectation pass and one sampling pass -- are timed at the same sizes so
// their share of a circuit's runtime can be stated.

#include <benchmark/benchmark.h>

#include <cmath>
#include <complex>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "common/checked_math.h"
#include "common/thread_count.h"
#include "engines/dense/apply_matrix.h"
#include "engines/dense/sample.h"
#include "engines/dense/state_vector.h"
#include "kernels/dense_kernels.h"
#include "menos_benchmark_environment.h"

namespace {

using menos::common::checked_mul;
using menos::common::checked_pow2;
using menos::engines::dense::apply_matrix;
using menos::engines::dense::initialize;
using menos::engines::dense::sample;
using menos::engines::dense::StateVector;

// Repetitions per data point (owner, Phase 15 Q1: 10 repeats per point).
constexpr int kRepetitions = 10;

// Shots per sampling pass, as in the CLI's default job size.
constexpr std::uint64_t kShots = 1024;

// A generic 2-qubit unitary (not a named gate) so this measures the same
// general Kronecker/permute kernel any gate ultimately uses, not a
// specialized fast path.
std::vector<std::complex<double>> generic_two_qubit_matrix() {
    // A fixed, arbitrary unitary: a real orthogonal permutation-plus-sign
    // matrix, valid regardless of state content (never checked here --
    // benchmarks are not correctness tests).
    return {
        {0, 0},
        {1, 0},
        {0, 0},
        {0, 0},
        {1, 0},
        {0, 0},
        {0, 0},
        {0, 0},
        {0, 0},
        {0, 0},
        {0, 0},
        {-1, 0},
        {0, 0},
        {0, 0},
        {-1, 0},
        {0, 0},
    };
}

// A generic dense 1-qubit unitary (every entry nonzero, not a named gate):
// the rotation exp(-i * 0.3 * Y) followed by a phase on |1>.
std::vector<std::complex<double>> generic_one_qubit_matrix() {
    const double c = std::cos(0.3);
    const double s = std::sin(0.3);
    const std::complex<double> phase = std::polar(1.0, 0.7);
    return {{c, 0}, {-s, 0}, phase * s, phase * c};
}

// The n-qubit state for one data point, or nullopt after marking the point
// skipped when 2^n amplitudes cannot be represented or safely allocated.
std::optional<StateVector> allocate_state(benchmark::State& state, unsigned n) {
    std::uint64_t required_bytes = 0;
    try {
        required_bytes = checked_mul(checked_pow2(n), sizeof(std::complex<double>));
    } catch (const menos::common::ResourceExceeded&) {
        state.SkipWithError("n too large to represent 2^n amplitudes");
        return std::nullopt;
    }
    if (required_bytes > menos_benchmark::safe_allocation_budget_bytes()) {
        state.SkipWithMessage("2^n amplitudes would exceed this machine's safe allocation budget");
        return std::nullopt;
    }
    std::optional<StateVector> sv;
    sv.emplace(static_cast<int>(n), required_bytes);
    return sv;
}

// Fills sv with the uniform superposition, so a reduction cannot stop early:
// the sampler's cumulative walk only ends once every shot is placed, which
// on |0...0> would be after the first amplitude.
void make_uniform(StateVector& sv) {
    const auto amps = sv.amplitudes();
    const double a = 1.0 / std::sqrt(static_cast<double>(amps.size()));
    for (std::complex<double>& amp : amps) {
        amp = {a, 0.0};
    }
}

// The 2-qubit gate on `threads` threads. The default-thread benchmarks keep
// their Phase 15 step-1 names so they compare directly with the baseline
// (docs/benchmarks/phase-15-baseline); the OneThread variants measure what the
// specialised kernels alone deliver (Phase 15 gate criterion 4).
void dense_gate_2q(benchmark::State& state, std::uint32_t threads) {
    const auto n = static_cast<unsigned>(state.range(0));
    std::optional<StateVector> sv = allocate_state(state, n);
    if (!sv) {
        return;
    }
    initialize(*sv, 0.0);
    const std::vector<std::complex<double>> matrix = generic_two_qubit_matrix();
    const std::vector<std::uint32_t> operands = {
        0, static_cast<std::uint32_t>(n - 1)};  // non-adjacent

    for (auto _ : state) {
        apply_matrix(sv->amplitudes(), matrix, operands, threads);
        benchmark::DoNotOptimize(sv->amplitudes().data());
    }
    state.counters["amplitudes"] = static_cast<double>(std::uint64_t{1} << n);
}

void dense_gate_1q(benchmark::State& state, std::uint32_t threads) {
    const auto n = static_cast<unsigned>(state.range(0));
    std::optional<StateVector> sv = allocate_state(state, n);
    if (!sv) {
        return;
    }
    initialize(*sv, 0.0);
    const std::vector<std::complex<double>> matrix = generic_one_qubit_matrix();
    const std::vector<std::uint32_t> operands = {static_cast<std::uint32_t>(n / 2)};

    for (auto _ : state) {
        apply_matrix(sv->amplitudes(), matrix, operands, threads);
        benchmark::DoNotOptimize(sv->amplitudes().data());
    }
    state.counters["amplitudes"] = static_cast<double>(std::uint64_t{1} << n);
}

void BM_DenseGateApplication(benchmark::State& state) {
    dense_gate_2q(state, menos::common::default_thread_count());
}

void BM_DenseGateApplication1Q(benchmark::State& state) {
    dense_gate_1q(state, menos::common::default_thread_count());
}

void BM_DenseGateApplicationOneThread(benchmark::State& state) {
    dense_gate_2q(state, 1);
}

void BM_DenseGateApplication1QOneThread(benchmark::State& state) {
    dense_gate_1q(state, 1);
}

// One Pauli term with X and Z on every qubit (so every amplitude pairs with
// a different one and every sign bit is live): one expectation pass.
void BM_DenseExpectationTerm(benchmark::State& state) {
    const auto n = static_cast<unsigned>(state.range(0));
    std::optional<StateVector> sv = allocate_state(state, n);
    if (!sv) {
        return;
    }
    make_uniform(*sv);
    const std::uint64_t all = (std::uint64_t{1} << n) - 1;

    for (auto _ : state) {
        benchmark::DoNotOptimize(
            menos::kernels::cpu::expectation_of_term(std::as_const(*sv).amplitudes(), all, all));
    }
    state.counters["amplitudes"] = static_cast<double>(std::uint64_t{1} << n);
}

// One sole-region sampling pass of kShots shots over every qubit.
void BM_DenseSample(benchmark::State& state) {
    const auto n = static_cast<unsigned>(state.range(0));
    std::optional<StateVector> sv = allocate_state(state, n);
    if (!sv) {
        return;
    }
    make_uniform(*sv);
    std::vector<std::pair<std::uint32_t, std::uint32_t>> targets;
    targets.reserve(n);
    for (std::uint32_t q = 0; q < n; ++q) {
        targets.emplace_back(q, q);
    }

    for (auto _ : state) {
        benchmark::DoNotOptimize(sample(*sv, kShots, /*seed=*/1, targets, n));
    }
    state.counters["amplitudes"] = static_cast<double>(std::uint64_t{1} << n);
}

void dense_sizes(benchmark::Benchmark* b) {
    for (const int n : {16, 18, 20, 22, 24, 26}) {
        b->Arg(n);
    }
    // DisplayAggregatesOnly, not ReportAggregatesOnly: the console shows the
    // aggregates while --benchmark_out keeps every repetition, so min and
    // max can be computed from the file.
    b->Unit(benchmark::kMillisecond)->Repetitions(kRepetitions)->DisplayAggregatesOnly(true);
}

BENCHMARK(BM_DenseGateApplication)->Apply(dense_sizes);
BENCHMARK(BM_DenseGateApplication1Q)->Apply(dense_sizes);
BENCHMARK(BM_DenseGateApplicationOneThread)->Apply(dense_sizes);
BENCHMARK(BM_DenseGateApplication1QOneThread)->Apply(dense_sizes);
BENCHMARK(BM_DenseExpectationTerm)->Apply(dense_sizes);
BENCHMARK(BM_DenseSample)->Apply(dense_sizes);

}  // namespace
