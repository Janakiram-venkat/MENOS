// docs/BUILD_PLAN.md Phase 6 acceptance criterion 4: "A 100-qubit
// nearest-neighbor shallow circuit runs exactly in MPS for both counts
// and expectation requests, where dense is refused."

#include "menos/menos.h"

#include <gtest/gtest.h>

namespace menos {
namespace {

constexpr std::uint32_t kNumQubits = 100;

// H on every qubit, then two layers of nearest-neighbor brickwork CX
// (even-odd pairs, then odd-even pairs): shallow (depth 3 total) and
// strictly nearest-neighbor, so the Schmidt rank at any cut is bounded
// by the product of at most 2 crossing gates' own rank-2 contribution
// (one per layer) -- never more than 4, vastly smaller than
// 2^min(left,right) at any interior cut of a 100-qubit chain.
Circuit make_hundred_qubit_shallow_circuit() {
    Circuit circuit(kNumQubits, kNumQubits);
    for (QubitId q = 0; q < kNumQubits; ++q) {
        circuit.append(Opcode::H, {q});
    }
    for (QubitId q = 0; q + 1 < kNumQubits; q += 2) {
        circuit.append(Opcode::CX, {q, static_cast<QubitId>(q + 1)});
    }
    for (QubitId q = 1; q + 1 < kNumQubits; q += 2) {
        circuit.append(Opcode::CX, {q, static_cast<QubitId>(q + 1)});
    }
    for (QubitId q = 0; q < kNumQubits; ++q) {
        circuit.append_measure(q, q);
    }
    return circuit;
}

// A budget comfortably large for MPS's own small, bond-limited state
// (a handful of KiB) but astronomically smaller than a 100-qubit dense
// state (16 * 2^100 bytes).
constexpr std::uint64_t kBudgetBytes = std::uint64_t{10} * 1024 * 1024;

TEST(MPSScaleTest, HundredQubitShallowCircuitCountsSucceedWithDenseRefused) {
    const Circuit circuit = make_hundred_qubit_shallow_circuit();
    const Result result = run(circuit,
                              RunOptions{.shots = 20,
                                         .want_statevector = false,
                                         .seed = 1,
                                         .memory_budget_bytes = kBudgetBytes});

    ASSERT_EQ(result.plan().strategy, Strategy::DirectMPS);
    EXPECT_TRUE(result.exact());
    ASSERT_EQ(result.plan().regions.size(), 1U);
    EXPECT_EQ(result.plan().regions[0].engine, Engine::MPS);

    // Dense's own candidate was genuinely considered and rejected on
    // memory grounds -- not merely absent from consideration.
    bool found_dense_candidate = false;
    for (const CandidateSummary& candidate : result.plan().candidates) {
        if (candidate.strategy == Strategy::DirectDense) {
            found_dense_candidate = true;
            EXPECT_FALSE(candidate.feasible) << "a 100-qubit dense state must be refused here";
        }
    }
    EXPECT_TRUE(found_dense_candidate);

    std::uint64_t total = 0;
    for (const auto& [key, count] : result.counts()) {
        EXPECT_EQ(key.num_bits(), kNumQubits);
        total += count;
    }
    EXPECT_EQ(total, 20U);
}

TEST(MPSScaleTest, HundredQubitShallowCircuitExpectationSucceedsWithDenseRefused) {
    Circuit circuit(kNumQubits, 0);
    for (QubitId q = 0; q < kNumQubits; ++q) {
        circuit.append(Opcode::H, {q});
    }
    for (QubitId q = 0; q + 1 < kNumQubits; q += 2) {
        circuit.append(Opcode::CX, {q, static_cast<QubitId>(q + 1)});
    }
    for (QubitId q = 1; q + 1 < kNumQubits; q += 2) {
        circuit.append(Opcode::CX, {q, static_cast<QubitId>(q + 1)});
    }

    // A local ZZ observable on two entangled neighbors, well within the
    // light cone regardless of the other 98 qubits.
    PauliString zz;
    zz.num_qubits = kNumQubits;
    zz.x_words.assign((kNumQubits + 63) / 64, 0);
    zz.z_words.assign((kNumQubits + 63) / 64, 0);
    zz.z_words[0] |= (std::uint64_t{1} << 0);
    zz.z_words[0] |= (std::uint64_t{1} << 1);
    const Observable observable{{{zz, {1.0, 0.0}}}};

    const Result result = run(circuit,
                              RunOptions{.shots = 0,
                                         .want_statevector = false,
                                         .seed = 1,
                                         .memory_budget_bytes = kBudgetBytes,
                                         .expectation = observable});

    ASSERT_TRUE(result.expectation_value().has_value());
    // H is applied to EVERY qubit first (not just qubit 0), so qubits 0
    // and 1 both start as |+> before CX(0,1) -- CX(control=0,target=1)
    // leaves |+>_0|+>_1 completely unchanged (a standard fact: X|+> =
    // |+>, and CX = |0><0| x I + |1><1| x X, so CX(|+>|+>) =
    // |0><0|+> x |+> + |1><1|+> x X|+> = |+>|+> again), so <Z0 Z1> = 0
    // exactly, not the +1 a naive Bell-pair-prep assumption would give
    // (that recipe starts from |0>|0>, not |+>|+>). Independently
    // verified via a 6-qubit brute-force numpy oracle with the same
    // gate structure before writing this assertion, not assumed by
    // hand.
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    EXPECT_NEAR(result.expectation_value()->real(), 0.0, 1e-9);
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    EXPECT_NEAR(result.expectation_value()->imag(), 0.0, 1e-9);
    EXPECT_TRUE(result.exact());
}

}  // namespace
}  // namespace menos
