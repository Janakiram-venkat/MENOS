#include "engines/mps/sampling.h"

#include <gtest/gtest.h>

#include <cmath>
#include <complex>
#include <map>
#include <type_traits>
#include <variant>
#include <vector>

#include "common/rng.h"
#include "engines/dense/apply_matrix.h"
#include "engines/dense/state_vector.h"
#include "engines/mps/apply_gate.h"
#include "ir/gate_table.h"
#include "menos/circuit.h"

namespace menos::engines::mps {
namespace {

using Complex = std::complex<double>;

std::vector<Complex> run_on_dense(const Circuit& circuit) {
    dense::StateVector state(static_cast<int>(circuit.num_qubits()), std::uint64_t{1} << 30);
    dense::initialize(state, 0.0);
    for (const Operation& op : circuit.operations()) {
        std::visit(
            [&](const auto& o) {
                using T = std::decay_t<decltype(o)>;
                if constexpr (std::is_same_v<T, GateOp>) {
                    const std::vector<Complex> matrix = ir::generate_matrix(o.opcode, o.params);
                    dense::apply_matrix(state.amplitudes(), matrix, o.qubits);
                }
            },
            op);
    }
    return std::vector<Complex>(state.amplitudes().begin(), state.amplitudes().end());
}

MPSState run_on_mps(const Circuit& circuit) {
    MPSState state(circuit.num_qubits());
    const AccuracyPolicy exact;
    TruncationStats stats;
    for (const Operation& op : circuit.operations()) {
        std::visit(
            [&](const auto& o) {
                using T = std::decay_t<decltype(o)>;
                if constexpr (std::is_same_v<T, GateOp>) {
                    const std::vector<Complex> matrix = ir::generate_matrix(o.opcode, o.params);
                    apply_gate(state, matrix, o.qubits, exact, stats);
                }
            },
            op);
    }
    return state;
}

// Chi-square against the dense oracle's own exact Born-rule
// probabilities (CLAUDE.md testing rules): the same 0.001-significance
// critical value tests/engines/dense/sample_test.cpp's own
// ChiSquareAgainstExactProbabilities test already derived and justified
// for df=3 (4 outcomes), reused here rather than re-derived, since this
// test also has 4 outcomes (2 qubits).
TEST(SamplingTest, ChiSquareAgainstDenseOracleProbabilities) {
    Circuit circuit(2, 2);
    circuit.append(Opcode::RY, {0}, {0.9});
    circuit.append(Opcode::RY, {1}, {1.7});
    circuit.append(Opcode::CX, {0, 1});
    circuit.append_measure(0, 1);  // qubit 0 -> clbit 1
    circuit.append_measure(1, 0);  // qubit 1 -> clbit 0 (swapped mapping)

    const std::vector<Complex> dense_amplitudes = run_on_dense(circuit);
    const MPSState mps_state = run_on_mps(circuit);

    const std::vector<std::pair<QubitId, ClbitId>> measure_targets = {{0, 1}, {1, 0}};
    const MPSCountsSampler sampler(mps_state, measure_targets, /*bitstring_width=*/2);

    constexpr std::uint64_t shots = 100'000;
    common::Rng rng(31415);
    std::map<BitString, std::uint64_t> counts;
    for (std::uint64_t shot = 0; shot < shots; ++shot) {
        counts[sampler.sample_one(rng)] += 1;
    }

    // expected[key], key = 2*q1 + q0 (little-endian amplitude index,
    // matching dense_amplitudes' own indexing), but counts are keyed by
    // BitString bits (clbit 1 = qubit 0, clbit 0 = qubit 1) -- the
    // measure_targets mapping above -- so build expected directly by
    // that same clbit convention.
    double expected[4] = {0.0, 0.0, 0.0, 0.0};
    for (std::uint64_t amp_index = 0; amp_index < 4; ++amp_index) {
        const bool q0 = (amp_index & 1) != 0;
        const bool q1 = (amp_index & 2) != 0;
        const std::uint64_t clbit_key =
            (static_cast<std::uint64_t>(q1) << 0) | (static_cast<std::uint64_t>(q0) << 1);
        expected[clbit_key] += std::norm(dense_amplitudes[amp_index]) * static_cast<double>(shots);
    }

    double chi_square = 0.0;
    for (std::uint64_t key = 0; key < 4; ++key) {
        BitString bitstring(2);
        bitstring.set(0, (key & 1) != 0);
        bitstring.set(1, (key & 2) != 0);
        const auto it = counts.find(bitstring);
        const double observed = (it == counts.end()) ? 0.0 : static_cast<double>(it->second);
        const double diff = observed - expected[key];
        chi_square += diff * diff / expected[key];
    }

    constexpr double kChiSquareCriticalDf3P0001 = 16.2662;
    EXPECT_LT(chi_square, kChiSquareCriticalDf3P0001)
        << "chi_square=" << chi_square
        << " (would reject the null hypothesis that MPSCountsSampler matches "
           "the exact Born-rule probabilities)";
}

// The sampler copies the state at construction and moves its own copy's
// center to site 0 -- the original MPSState passed in must be
// unaffected, and every draw must come from the same distribution (no
// state leaking between shots).
TEST(SamplingTest, SamplerDoesNotMutateOriginalStateAndIsConsistentAcrossShots) {
    Circuit circuit(2, 0);
    circuit.append(Opcode::H, {0});
    circuit.append(Opcode::CX, {0, 1});
    const MPSState original = run_on_mps(circuit);
    const std::size_t original_center = original.orthogonality_center();
    const std::size_t original_bond = original.bond_dimension(0);

    const std::vector<std::pair<QubitId, ClbitId>> measure_targets = {{0, 0}, {1, 1}};
    const MPSCountsSampler sampler(original, measure_targets, /*bitstring_width=*/2);

    EXPECT_EQ(original.orthogonality_center(), original_center);
    EXPECT_EQ(original.bond_dimension(0), original_bond);

    common::Rng rng(7);
    bool saw_00 = false;
    bool saw_11 = false;
    bool saw_other = false;
    for (int i = 0; i < 200; ++i) {
        const BitString outcome = sampler.sample_one(rng);
        if (!outcome.test(0) && !outcome.test(1)) {
            saw_00 = true;
        } else if (outcome.test(0) && outcome.test(1)) {
            saw_11 = true;
        } else {
            saw_other = true;
        }
    }
    EXPECT_TRUE(saw_00);
    EXPECT_TRUE(saw_11);
    EXPECT_FALSE(saw_other) << "Bell state must never yield 01 or 10";
}

}  // namespace
}  // namespace menos::engines::mps
