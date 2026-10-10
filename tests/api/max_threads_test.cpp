// RunOptions::max_threads (Phase 15, docs/adr/0019 section 3.3): its
// validation, and the guarantee that the thread count never changes a result.
// The dense, density-matrix and trajectory engines all apply gates through the
// threaded CPU kernel; for each, a job run with max_threads = 1, 2, 3 and
// unset (every hardware thread) must give bitwise-identical statevectors,
// counts and expectation values. Each circuit is large enough that its gates
// are split across threads, so the comparison is not of four serial runs.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <complex>
#include <cstdint>
#include <optional>
#include <random>
#include <vector>

#include "common/error.h"
#include "common/thread_count.h"
#include "kernels/dense_kernels.h"
#include "menos/menos.h"

namespace menos {
namespace {

using Complex = std::complex<double>;
using Matrix = std::vector<Complex>;

// The smallest qubit count whose 2-qubit gates (2^(n-2) groups) reach the
// threading cut-off, so every gate in these circuits is split.
std::uint32_t threaded_qubit_count() {
    std::uint32_t n = 2;
    while ((std::size_t{1} << (n - 2)) < kernels::cpu::kParallelMinGroups) {
        ++n;
    }
    return n;
}

// Layers of random single-qubit rotations, a CX ladder, and one 3-qubit
// Toffoli per layer, so the 1-qubit, 2-qubit and generic kernels all run.
// A layer count and an RNG seed, named at every call.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
void append_layers(Circuit& circuit, std::uint32_t layers, std::uint64_t seed) {
    // NOLINTEND(bugprone-easily-swappable-parameters)
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> angle(0.0, 6.283185307179586);
    const std::uint32_t n = circuit.num_qubits();
    for (std::uint32_t layer = 0; layer < layers; ++layer) {
        for (QubitId q = 0; q < n; ++q) {
            circuit.append(Opcode::RY, {q}, {angle(rng)});
            circuit.append(Opcode::RZ, {q}, {angle(rng)});
        }
        for (QubitId q = layer % 2; q + 1 < n; q += 2) {
            circuit.append(Opcode::CX, {q, q + 1});
        }
        circuit.append(Opcode::CCX, {n - 1, 0, n / 2});
    }
}

Observable z0_plus_x_last(std::uint32_t n) {
    Observable observable;
    if (n == 0) {
        return observable;  // no qubit to place X on; every caller has n >= 2
    }
    observable.terms.emplace_back(PauliString{n, {0}, {1}}, Complex{0.75, 0.0});
    observable.terms.emplace_back(PauliString{n, {std::uint64_t{1} << (n - 1)}, {0}},
                                  Complex{-0.5, 0.0});
    return observable;
}

std::vector<std::optional<std::uint32_t>> thread_settings() {
    return {std::uint32_t{1}, std::uint32_t{2}, std::uint32_t{3}, std::nullopt};
}

// Bit-for-bit equality of the real and imaginary parts (so +0.0 and -0.0, or
// two NaN payloads, are different), not floating-point ==.
bool same_bits(const Complex& a, const Complex& b) {
    return std::bit_cast<std::uint64_t>(a.real()) == std::bit_cast<std::uint64_t>(b.real()) &&
           std::bit_cast<std::uint64_t>(a.imag()) == std::bit_cast<std::uint64_t>(b.imag());
}

bool same_bytes(const std::vector<Complex>& a, const std::vector<Complex>& b) {
    return std::ranges::equal(a, b, same_bits);
}

// Runs circuit once per thread setting and requires every result to equal the
// single-threaded one bit for bit, on the expected engine.
void expect_results_independent_of_threads(const Circuit& circuit,
                                           RunOptions options,
                                           Engine expected_engine) {
    options.max_threads = 1;
    const Result reference = run(circuit, options);
    ASSERT_EQ(reference.plan().regions.size(), 1U);
    ASSERT_EQ(reference.plan().regions[0].engine, expected_engine);
    for (const std::optional<std::uint32_t> threads : thread_settings()) {
        options.max_threads = threads;
        const Result result = run(circuit, options);
        const std::string label = threads.has_value() ? std::to_string(*threads) : "unset";
        EXPECT_EQ(result.plan().regions[0].engine, expected_engine) << label;
        EXPECT_EQ(result.counts(), reference.counts()) << "max_threads=" << label;
        const auto& state = result.statevector();
        const auto& reference_state = reference.statevector();
        ASSERT_EQ(state.has_value(), reference_state.has_value());
        if (state.has_value() && reference_state.has_value()) {
            EXPECT_TRUE(same_bytes(*state, *reference_state)) << "max_threads=" << label;
        }
        const auto& value = result.expectation_value();
        const auto& reference_value = reference.expectation_value();
        ASSERT_EQ(value.has_value(), reference_value.has_value());
        if (value.has_value() && reference_value.has_value()) {
            EXPECT_TRUE(same_bits(*value, *reference_value)) << "max_threads=" << label;
        }
    }
}

// rho -> (1 - p) rho + p I/2.
std::vector<Matrix> depolarizing_1q(double p) {
    const std::array<Matrix, 4> paulis = {
        Matrix{1.0, 0.0, 0.0, 1.0},
        Matrix{0.0, 1.0, 1.0, 0.0},
        Matrix{0.0, Complex{0.0, -1.0}, Complex{0.0, 1.0}, 0.0},
        Matrix{1.0, 0.0, 0.0, -1.0},
    };
    std::vector<Matrix> kraus;
    for (std::size_t i = 0; i < 4; ++i) {
        const double w = i == 0 ? std::sqrt(1.0 - 3.0 * p / 4.0) : std::sqrt(p / 4.0);
        Matrix m = paulis[i];
        for (Complex& v : m) {
            v *= w;
        }
        kraus.push_back(m);
    }
    return kraus;
}

TEST(MaxThreadsTest, ZeroIsRefused) {
    Circuit circuit(1, 1);
    circuit.append(Opcode::H, {0});
    circuit.append_measure(0, 0);
    RunOptions options{.shots = 10, .seed = 1};
    options.max_threads = 0;
    EXPECT_THROW(static_cast<void>(run(circuit, options)), common::InvalidCircuit);
}

TEST(MaxThreadsTest, ACapAboveTheHardwareCountRunsAndMatchesOneThread) {
    Circuit circuit(3, 0);
    append_layers(circuit, 2, 17);
    RunOptions options{.shots = 0, .want_statevector = true, .seed = 1};
    options.max_threads = 1;
    const Result one = run(circuit, options);
    options.max_threads = common::default_thread_count() + 7;
    const Result capped = run(circuit, options);
    const auto& capped_state = capped.statevector();
    const auto& one_state = one.statevector();
    ASSERT_TRUE(capped_state.has_value() && one_state.has_value());
    EXPECT_TRUE(capped_state.has_value() && one_state.has_value() &&
                same_bytes(*capped_state, *one_state));
}

TEST(MaxThreadsTest, DenseResultsAreBitwiseIndependentOfThreadCount) {
    const std::uint32_t n = threaded_qubit_count() + 1;
    Circuit circuit(n, n);
    append_layers(circuit, 3, 1521);
    for (QubitId q = 0; q < n; ++q) {
        circuit.append_measure(q, q);
    }
    RunOptions options{.shots = 2000, .want_statevector = true, .seed = 99};
    options.expectation = z0_plus_x_last(n);
    expect_results_independent_of_threads(circuit, options, Engine::Dense);
}

TEST(MaxThreadsTest, DensityMatrixResultsAreBitwiseIndependentOfThreadCount) {
    // The density matrix of m qubits is a 2m-qubit vector, and its 1-qubit
    // channels are 2-operand superoperators on it.
    const std::uint32_t m = (threaded_qubit_count() + 2) / 2;
    Circuit circuit(m, m);
    append_layers(circuit, 2, 1522);
    for (QubitId q = 0; q < m; ++q) {
        circuit.append_channel({q}, depolarizing_1q(0.05));
    }
    for (QubitId q = 0; q < m; ++q) {
        circuit.append_measure(q, q);
    }
    RunOptions options{.shots = 2000, .seed = 7};
    options.expectation = z0_plus_x_last(m);
    expect_results_independent_of_threads(circuit, options, Engine::DensityMatrix);
}

TEST(MaxThreadsTest, TrajectoryResultsAreBitwiseIndependentOfThreadCount) {
    // A memory budget too small for the density matrix leaves trajectories as
    // the only plan.
    const std::uint32_t n = threaded_qubit_count() + 1;
    Circuit circuit(n, n);
    // Two layers: the CX ladder alternates between even and odd pairs, so one
    // layer alone would leave the circuit in pieces and the plan Factorized.
    append_layers(circuit, 2, 1523);
    for (QubitId q = 0; q < n; ++q) {
        circuit.append_channel({q}, depolarizing_1q(0.05));
    }
    for (QubitId q = 0; q < n; ++q) {
        circuit.append_measure(q, q);
    }
    RunOptions options{.shots = 8, .seed = 11};
    options.noise_method = NoiseMethod::Auto;
    options.memory_budget_bytes = std::uint64_t{64} << (n + 1);
    expect_results_independent_of_threads(circuit, options, Engine::Trajectory);
}

}  // namespace
}  // namespace menos
