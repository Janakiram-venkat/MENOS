// Multi-threaded gate application must not change a single bit (docs/adr/0019
// section 3.2): every amplitude group is computed by exactly one thread with
// the same arithmetic as on one thread, so the state is bitwise identical for
// every thread count. Each case runs at a size at or above the threading
// cut-off, so the work is really split rather than the test passing on one thread.

#include <gtest/gtest.h>

#include <algorithm>
#include <complex>
#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

#include "common/thread_count.h"
#include "kernels/dense_kernels.h"

namespace menos::kernels::cpu {
namespace {

using Complex = std::complex<double>;

std::vector<Complex> random_vector(std::size_t size, std::mt19937_64& rng) {
    std::uniform_real_distribution<double> dist(-1.0, 1.0);
    std::vector<Complex> v(size);
    for (Complex& c : v) {
        c = Complex{dist(rng), dist(rng)};
    }
    return v;
}

// The smallest qubit count at which a k-qubit gate reaches the threading
// cut-off, so a 2- and 3-thread request really splits the work.
std::uint32_t threaded_qubit_count(std::uint32_t k) {
    std::uint32_t n = k;
    while ((std::size_t{1} << (n - k)) < kParallelMinGroups) {
        ++n;
    }
    return std::max(n + 1, k + 2);
}

// Thread counts to compare with 1: 2, 3 (an odd split) and every hardware
// thread, deduplicated; a machine with one hardware thread still runs 2 and 3,
// because the kernel takes the count it is given.
std::vector<std::uint32_t> thread_counts() {
    std::vector<std::uint32_t> counts = {2, 3, common::default_thread_count()};
    std::sort(counts.begin(), counts.end());
    counts.erase(std::unique(counts.begin(), counts.end()), counts.end());
    return counts;
}

// Operand count, RNG seed and kernel choice, named at every call.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
void expect_thread_count_invariant(std::uint32_t k, std::uint64_t seed, bool generic) {
    // NOLINTEND(bugprone-easily-swappable-parameters)
    std::mt19937_64 rng(seed);
    const std::uint32_t n = threaded_qubit_count(k);
    const std::vector<Complex> initial = random_vector(std::size_t{1} << n, rng);
    const std::size_t dim = std::size_t{1} << k;
    const std::vector<Complex> matrix = random_vector(dim * dim, rng);
    // Operands in a non-sorted order, spread across the index; distinct for
    // every n >= k + 2, which threaded_qubit_count guarantees.
    const std::vector<std::uint32_t> pattern = {n - 1, 0, n / 2, 1};
    const std::vector<std::uint32_t> operands(pattern.begin(), pattern.begin() + k);
    const auto apply = [&](std::vector<Complex>& state, std::uint32_t threads) {
        if (generic) {
            apply_matrix_generic(state, matrix, operands, threads);
        } else {
            apply_matrix(state, matrix, operands, threads);
        }
    };
    std::vector<Complex> reference = initial;
    apply(reference, 1);
    for (const std::uint32_t threads : thread_counts()) {
        std::vector<Complex> threaded = initial;
        apply(threaded, threads);
        ASSERT_EQ(
            std::memcmp(reference.data(), threaded.data(), reference.size() * sizeof(Complex)), 0)
            << "k=" << k << " n=" << n << " threads=" << threads << (generic ? " (generic)" : "");
    }
}

TEST(ThreadedKernelsTest, OneQubitKernelIsBitwiseIdenticalForEveryThreadCount) {
    expect_thread_count_invariant(1, 1511, /*generic=*/false);
}

TEST(ThreadedKernelsTest, TwoQubitKernelIsBitwiseIdenticalForEveryThreadCount) {
    expect_thread_count_invariant(2, 1512, /*generic=*/false);
}

TEST(ThreadedKernelsTest, GenericKernelIsBitwiseIdenticalForEveryThreadCount) {
    for (std::uint32_t k = 1; k <= 4; ++k) {
        expect_thread_count_invariant(k, 1513 + k, /*generic=*/true);
    }
}

TEST(ThreadedKernelsTest, ZeroThreadsIsRefused) {
    std::vector<Complex> state(4, Complex{0.5, 0.0});
    const std::vector<Complex> identity = {{1.0, 0.0}, {0.0, 0.0}, {0.0, 0.0}, {1.0, 0.0}};
    const std::vector<std::uint32_t> operands = {0};
    EXPECT_ANY_THROW(apply_matrix(state, identity, operands, 0));
}

}  // namespace
}  // namespace menos::kernels::cpu
