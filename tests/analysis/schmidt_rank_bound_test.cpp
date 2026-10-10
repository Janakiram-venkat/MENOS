#include "analysis/schmidt_rank_bound.h"

#include <gtest/gtest.h>

#include <cmath>
#include <complex>
#include <vector>

namespace menos::analysis {
namespace {

using Complex = std::complex<double>;

// Every expected rank below was independently computed by a Python/numpy
// oracle (reshape the gate matrix per the standard operator-Schmidt
// construction, take the SVD, count singular values above
// numpy.linalg.matrix_rank's own default cutoff) before this test was
// written, not derived from the C++ implementation under test.

TEST(SchmidtRankBoundTest, CXHasOperatorSchmidtRankTwo) {
    Circuit circuit(2, 0);
    circuit.append(Opcode::CX, {0, 1});
    const SchmidtRankBound bound =
        compute_schmidt_rank_bound(circuit.num_qubits(), circuit.operations());
    ASSERT_EQ(bound.bound_per_cut.size(), 1u);
    EXPECT_EQ(bound.bound_per_cut[0], 2u);
    EXPECT_EQ(bound.max_bound(), 2u);
}

TEST(SchmidtRankBoundTest, ProductStateCircuitHasTrivialBound) {
    Circuit circuit(2, 0);
    circuit.append(Opcode::H, {0});
    circuit.append(Opcode::H, {1});
    const SchmidtRankBound bound =
        compute_schmidt_rank_bound(circuit.num_qubits(), circuit.operations());
    ASSERT_EQ(bound.bound_per_cut.size(), 1u);
    EXPECT_EQ(bound.bound_per_cut[0], 1u);
    EXPECT_EQ(bound.max_bound(), 1u);
}

// SWAP's own operator-Schmidt-rank is 4 (verified by the oracle above),
// but a single cut with one qubit on each side can carry at most
// 2^min(1,1) = 2 -- the physical-dimension cap must win over the raw
// per-gate rank.
TEST(SchmidtRankBoundTest, SwapGateBoundIsCappedByPhysicalDimension) {
    Circuit circuit(2, 0);
    circuit.append(Opcode::SWAP, {0, 1});
    const SchmidtRankBound bound =
        compute_schmidt_rank_bound(circuit.num_qubits(), circuit.operations());
    ASSERT_EQ(bound.bound_per_cut.size(), 1u);
    EXPECT_EQ(bound.bound_per_cut[0], 2u);
}

// Two rank-2 CX gates crossing the same cut would multiply to 4 if
// uncapped; the physical cap (2^min(1,1) = 2) must still win.
TEST(SchmidtRankBoundTest, RepeatedCrossingGatesStayCappedByPhysicalDimension) {
    Circuit circuit(2, 0);
    circuit.append(Opcode::CX, {0, 1});
    circuit.append(Opcode::CX, {1, 0});
    const SchmidtRankBound bound =
        compute_schmidt_rank_bound(circuit.num_qubits(), circuit.operations());
    ASSERT_EQ(bound.bound_per_cut.size(), 1u);
    EXPECT_EQ(bound.bound_per_cut[0], 2u);
}

// CCX(control1=0, control2=1, target=2) crosses two cuts with two
// genuinely different operand bipartitions ({0}|{1,2} at cut 0,
// {0,1}|{2} at cut 1) -- the oracle gives rank 2 for both, exercising
// the per-cut-recomputation path even though the two numeric answers
// happen to coincide.
TEST(SchmidtRankBoundTest, CCXCrossesBothCutsWithVerifiedRanks) {
    Circuit circuit(3, 0);
    circuit.append(Opcode::CCX, {0, 1, 2});
    const SchmidtRankBound bound =
        compute_schmidt_rank_bound(circuit.num_qubits(), circuit.operations());
    ASSERT_EQ(bound.bound_per_cut.size(), 2u);
    EXPECT_EQ(bound.bound_per_cut[0], 2u);
    EXPECT_EQ(bound.bound_per_cut[1], 2u);
    EXPECT_EQ(bound.max_bound(), 2u);
}

// A 2-operand unitary on non-adjacent qubits {0, 2} (qubit 1 untouched)
// crosses both intermediate cuts (0 and 1) with the SAME operand
// bipartition ({operand 0}|{operand 1}) each time, since it has only
// two operands -- verifies the min/max qubit crossing-range logic for
// a gap, independent of the per-cut bipartition-recomputation tested
// above by CCX.
TEST(SchmidtRankBoundTest, NonAdjacentOperandsCrossEveryIntermediateCut) {
    Circuit circuit(3, 0);
    const std::vector<Complex> cx_matrix = {Complex{1, 0},
                                            Complex{0, 0},
                                            Complex{0, 0},
                                            Complex{0, 0},
                                            Complex{0, 0},
                                            Complex{1, 0},
                                            Complex{0, 0},
                                            Complex{0, 0},
                                            Complex{0, 0},
                                            Complex{0, 0},
                                            Complex{0, 0},
                                            Complex{1, 0},
                                            Complex{0, 0},
                                            Complex{0, 0},
                                            Complex{1, 0},
                                            Complex{0, 0}};
    circuit.append_unitary({0, 2}, cx_matrix);
    const SchmidtRankBound bound =
        compute_schmidt_rank_bound(circuit.num_qubits(), circuit.operations());
    ASSERT_EQ(bound.bound_per_cut.size(), 2u);
    EXPECT_EQ(bound.bound_per_cut[0], 2u);
    EXPECT_EQ(bound.bound_per_cut[1], 2u);
}

TEST(SchmidtRankBoundTest, SingleQubitCircuitHasNoCutsAndTrivialMaxBound) {
    Circuit circuit(1, 0);
    circuit.append(Opcode::H, {0});
    const SchmidtRankBound bound =
        compute_schmidt_rank_bound(circuit.num_qubits(), circuit.operations());
    EXPECT_TRUE(bound.bound_per_cut.empty());
    EXPECT_EQ(bound.max_bound(), 1u);
}

TEST(SchmidtRankBoundTest, MaxBoundIsTheLargestAcrossCuts) {
    Circuit circuit(3, 0);
    circuit.append(Opcode::CX, {0, 1});  // crosses cut 0 only, rank 2
    const SchmidtRankBound bound =
        compute_schmidt_rank_bound(circuit.num_qubits(), circuit.operations());
    ASSERT_EQ(bound.bound_per_cut.size(), 2u);
    EXPECT_EQ(bound.bound_per_cut[0], 2u);
    EXPECT_EQ(bound.bound_per_cut[1], 1u);
    EXPECT_EQ(bound.max_bound(), 2u);
}

}  // namespace
}  // namespace menos::analysis
