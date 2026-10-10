#include "engines/mps/apply_gate.h"

#include <gtest/gtest.h>

#include <complex>
#include <type_traits>
#include <variant>
#include <vector>

#include "engines/dense/apply_matrix.h"
#include "engines/dense/state_vector.h"
#include "ir/gate_table.h"
#include "menos/circuit.h"
#include "mps_test_support.h"

namespace menos::engines::mps {
namespace {

using Complex = std::complex<double>;
constexpr double kTol = 1e-9;
constexpr std::uint64_t kBudget = std::uint64_t{1} << 30;

// The independent oracle: engines::dense's own apply_matrix, the same
// production kernel every other engine's differential tests are
// checked against (e.g. tests/engines/stabilizer/stabilizer_engine_test.cpp).
std::vector<Complex> run_on_dense(const Circuit& circuit) {
    dense::StateVector state(static_cast<int>(circuit.num_qubits()), kBudget);
    dense::initialize(state, 0.0);
    for (const Operation& op : circuit.operations()) {
        std::visit(
            [&](const auto& o) {
                using T = std::decay_t<decltype(o)>;
                if constexpr (std::is_same_v<T, GateOp>) {
                    const std::vector<Complex> matrix = ir::generate_matrix(o.opcode, o.params);
                    dense::apply_matrix(state.amplitudes(), matrix, o.qubits);
                } else if constexpr (std::is_same_v<T, UnitaryOp>) {
                    dense::apply_matrix(state.amplitudes(), o.matrix, o.qubits);
                }
            },
            op);
    }
    return std::vector<Complex>(state.amplitudes().begin(), state.amplitudes().end());
}

std::vector<Complex> run_on_mps(const Circuit& circuit, TruncationStats& stats) {
    MPSState state(circuit.num_qubits());
    const AccuracyPolicy exact;  // defaults to Exact.
    for (const Operation& op : circuit.operations()) {
        std::visit(
            [&](const auto& o) {
                using T = std::decay_t<decltype(o)>;
                if constexpr (std::is_same_v<T, GateOp>) {
                    const std::vector<Complex> matrix = ir::generate_matrix(o.opcode, o.params);
                    apply_gate(state, matrix, o.qubits, exact, stats);
                } else if constexpr (std::is_same_v<T, UnitaryOp>) {
                    apply_gate(state, o.matrix, o.qubits, exact, stats);
                }
            },
            op);
    }
    return reconstruct_statevector(state);
}

void expect_matches_dense_oracle(const Circuit& circuit) {
    const std::vector<Complex> expected = run_on_dense(circuit);
    TruncationStats stats;
    const std::vector<Complex> actual = run_on_mps(circuit, stats);
    EXPECT_TRUE(stats.discarded_weight_per_step.empty()) << "Exact policy must never truncate";
    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t i = 0; i < actual.size(); ++i) {
        EXPECT_NEAR(actual[i].real(), expected[i].real(), kTol) << "index " << i;
        EXPECT_NEAR(actual[i].imag(), expected[i].imag(), kTol) << "index " << i;
    }
}

TEST(ApplyGateTest, SingleHadamardMatchesDense) {
    Circuit circuit(1, 0);
    circuit.append(Opcode::H, {0});
    expect_matches_dense_oracle(circuit);
}

TEST(ApplyGateTest, AdjacentCXCreatesBellState) {
    Circuit circuit(2, 0);
    circuit.append(Opcode::H, {0});
    circuit.append(Opcode::CX, {0, 1});
    expect_matches_dense_oracle(circuit);
}

TEST(ApplyGateTest, ReversedOperandAdjacentCXMatchesDense) {
    // control=1, target=0 -- an adjacent pair, but qubits[0] is the
    // LARGER site index, exercising localize_matrix's operand-rank
    // permutation even without any SWAP-network gathering.
    Circuit circuit(2, 0);
    circuit.append(Opcode::H, {1});
    circuit.append(Opcode::CX, {1, 0});
    expect_matches_dense_oracle(circuit);
}

TEST(ApplyGateTest, NonAdjacentCXNeedsSwapNetwork) {
    // control=0, target=2, qubit 1 untouched -- the SWAP-gather/scatter
    // path.
    Circuit circuit(3, 0);
    circuit.append(Opcode::H, {0});
    circuit.append(Opcode::CX, {0, 2});
    expect_matches_dense_oracle(circuit);
}

TEST(ApplyGateTest, GHZChainAcrossFourQubitsMatchesDense) {
    Circuit circuit(4, 0);
    circuit.append(Opcode::H, {0});
    circuit.append(Opcode::CX, {0, 1});
    circuit.append(Opcode::CX, {1, 2});
    circuit.append(Opcode::CX, {2, 3});
    expect_matches_dense_oracle(circuit);
}

TEST(ApplyGateTest, AdjacentCCXMatchesDense) {
    Circuit circuit(3, 0);
    circuit.append(Opcode::H, {0});
    circuit.append(Opcode::H, {1});
    circuit.append(Opcode::CCX, {0, 1, 2});
    expect_matches_dense_oracle(circuit);
}

TEST(ApplyGateTest, NonAdjacentScatteredOperandCCXMatchesDense) {
    // Operands (0, 3, 1): non-adjacent, a gap (qubit 2), and not given
    // in ascending order -- stresses SWAP-gather and
    // localize_matrix's rank permutation together.
    Circuit circuit(4, 0);
    circuit.append(Opcode::H, {0});
    circuit.append(Opcode::H, {1});
    circuit.append(Opcode::H, {3});
    circuit.append(Opcode::CCX, {0, 3, 1});
    expect_matches_dense_oracle(circuit);
}

TEST(ApplyGateTest, NonAdjacentUnitaryOpMatchesDense) {
    Circuit circuit(3, 0);
    circuit.append(Opcode::H, {0});
    circuit.append(Opcode::H, {2});
    // A fixed, hand-picked 2-qubit unitary: |00>-><00|, |01>->i|01>,
    // |10>->|11>, |11>->i|10> -- a generalized permutation matrix
    // (unitary since every row/column has exactly one unit-modulus
    // entry), chosen to exercise genuinely complex amplitudes.
    const std::vector<Complex> matrix = {Complex{1.0, 0.0},
                                         Complex{0.0, 0.0},
                                         Complex{0.0, 0.0},
                                         Complex{0.0, 0.0},
                                         Complex{0.0, 0.0},
                                         Complex{0.0, 1.0},
                                         Complex{0.0, 0.0},
                                         Complex{0.0, 0.0},
                                         Complex{0.0, 0.0},
                                         Complex{0.0, 0.0},
                                         Complex{0.0, 0.0},
                                         Complex{1.0, 0.0},
                                         Complex{0.0, 0.0},
                                         Complex{0.0, 0.0},
                                         Complex{0.0, 1.0},
                                         Complex{0.0, 0.0}};
    circuit.append_unitary({0, 2}, matrix);
    expect_matches_dense_oracle(circuit);
}

TEST(ApplyGateTest, MixedCircuitWithInterleavedSingleQubitGatesMatchesDense) {
    Circuit circuit(4, 0);
    circuit.append(Opcode::H, {0});
    circuit.append(Opcode::S, {1});
    circuit.append(Opcode::CX, {0, 1});
    circuit.append(Opcode::T, {2});
    circuit.append(Opcode::CX, {1, 3});  // non-adjacent (skips qubit 2)
    circuit.append(Opcode::H, {2});
    circuit.append(Opcode::CCX, {3, 0, 2});  // non-adjacent, scattered
    circuit.append(Opcode::RZ, {1}, {0.7});
    expect_matches_dense_oracle(circuit);
}

// A 4-qubit GHZ chain has Schmidt rank exactly 2 at every cut. A
// generous cap (4, above the true maximum of 2) must never bind: no
// truncation, exact match with dense.
TEST(ApplyGateTest, GenerousMaxBondDimensionNeverTruncates) {
    Circuit circuit(4, 0);
    circuit.append(Opcode::H, {0});
    circuit.append(Opcode::CX, {0, 1});
    circuit.append(Opcode::CX, {1, 2});
    circuit.append(Opcode::CX, {2, 3});

    const std::vector<Complex> expected = run_on_dense(circuit);
    MPSState state(circuit.num_qubits());
    TruncationStats stats;
    AccuracyPolicy policy;
    policy.kind = AccuracyPolicyKind::Controlled;
    policy.tolerance = 0.0;
    policy.max_bond_dimension = 4;
    for (const Operation& op : circuit.operations()) {
        std::visit(
            [&](const auto& o) {
                using T = std::decay_t<decltype(o)>;
                if constexpr (std::is_same_v<T, GateOp>) {
                    const std::vector<Complex> matrix = ir::generate_matrix(o.opcode, o.params);
                    apply_gate(state, matrix, o.qubits, policy, stats);
                }
            },
            op);
    }
    EXPECT_TRUE(stats.discarded_weight_per_step.empty());
    const std::vector<Complex> actual = reconstruct_statevector(state);
    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t i = 0; i < actual.size(); ++i) {
        EXPECT_NEAR(actual[i].real(), expected[i].real(), kTol) << "index " << i;
        EXPECT_NEAR(actual[i].imag(), expected[i].imag(), kTol) << "index " << i;
    }
}

// The same GHZ chain with max_bond_dimension = 1 forces truncation at
// every entangling cut (true rank 2 capped to 1): discarded weight
// must be recorded, and the resulting (product) state must still be
// normalized -- a basic sanity check that truncation degrades
// gracefully rather than corrupting the representation. The specific
// error-vs-bound relationship is Phase 6's ADR-backed accumulated
// error bound, not asserted here.
TEST(ApplyGateTest, TightMaxBondDimensionTruncatesAndStaysNormalized) {
    Circuit circuit(4, 0);
    circuit.append(Opcode::H, {0});
    circuit.append(Opcode::CX, {0, 1});
    circuit.append(Opcode::CX, {1, 2});
    circuit.append(Opcode::CX, {2, 3});

    MPSState state(circuit.num_qubits());
    TruncationStats stats;
    AccuracyPolicy policy;
    policy.kind = AccuracyPolicyKind::Controlled;
    policy.tolerance = 0.0;
    policy.max_bond_dimension = 1;
    for (const Operation& op : circuit.operations()) {
        std::visit(
            [&](const auto& o) {
                using T = std::decay_t<decltype(o)>;
                if constexpr (std::is_same_v<T, GateOp>) {
                    const std::vector<Complex> matrix = ir::generate_matrix(o.opcode, o.params);
                    apply_gate(state, matrix, o.qubits, policy, stats);
                }
            },
            op);
    }
    EXPECT_FALSE(stats.discarded_weight_per_step.empty());
    const std::vector<Complex> actual = reconstruct_statevector(state);
    double norm_squared = 0.0;
    for (const Complex& amplitude : actual) {
        norm_squared += std::norm(amplitude);
    }
    EXPECT_NEAR(norm_squared, 1.0, kTol);
}

}  // namespace
}  // namespace menos::engines::mps
