// docs/adr/0013 Phase 9: cut finding, fragment generation, observable
// folding and the cost-model counts, tested below the engine and planner
// layers so a failure here is unambiguous about where it lives.

#include "analysis/cut.h"

#include <gtest/gtest.h>

#include <map>
#include <set>
#include <span>
#include <string>
#include <variant>

#include "common/pauli_row.h"

#include "ir/wire_cut_basis.h"

namespace menos::analysis {
namespace {

// Every test here guards its optional with ASSERT_TRUE(...has_value()) before
// reading it, but clang-tidy cannot trace through GoogleTest's early return,
// and flags .value() as readily as operator*. Suppressed once for the file
// rather than at ~25 individual sites, following the convention used across
// this codebase.
// NOLINTBEGIN(bugprone-unchecked-optional-access)

Circuit two_cluster_circuit() {
    // Qubits {0,1} interact, {2,3} interact, then qubit 0 migrates and
    // interacts with {2,3}. One cut on qubit 0 separates the two clusters.
    Circuit circuit(4, 0);
    circuit.append(Opcode::H, {0});
    circuit.append(Opcode::CX, {0, 1});
    circuit.append(Opcode::H, {2});
    circuit.append(Opcode::CX, {2, 3});
    circuit.append(Opcode::T, {1});
    circuit.append(Opcode::CZ, {0, 2});
    circuit.append(Opcode::CZ, {0, 3});
    return circuit;
}

TEST(CutTest, FindsACutThatSplitsAMigratingQubitIntoTwoFragments) {
    const Circuit circuit = two_cluster_circuit();
    const std::optional<CutPlan> maybe = find_cut_plan(circuit, kMaxCuts);
    ASSERT_TRUE(maybe.has_value());
    const CutPlan& plan = maybe.value();
    EXPECT_GE(plan.fragments.size(), 2U);
    EXPECT_GE(plan.cuts.size(), 1U);

    // Every operation must land in exactly one fragment: none lost, none
    // duplicated.
    std::size_t total_ops = 0;
    for (const CutFragment& fragment : plan.fragments) {
        total_ops += fragment.operations.size();
    }
    EXPECT_EQ(total_ops, circuit.operations().size());
}

// The cut qubit appears in two fragments -- measured on one side, prepared on
// the other. That pairing is the whole mechanism.
TEST(CutTest, EachCutHasExactlyOneMeasuredAndOnePreparedEndpoint) {
    const Circuit circuit = two_cluster_circuit();
    const std::optional<CutPlan> maybe = find_cut_plan(circuit, kMaxCuts);
    ASSERT_TRUE(maybe.has_value());
    const CutPlan& plan = maybe.value();

    for (std::size_t c = 0; c < plan.cuts.size(); ++c) {
        int measured = 0;
        int prepared = 0;
        for (const CutFragment& fragment : plan.fragments) {
            for (const CutEndpoint& e : fragment.measured) {
                if (e.cut_index == c) {
                    measured += 1;
                }
            }
            for (const CutEndpoint& e : fragment.prepared) {
                if (e.cut_index == c) {
                    prepared += 1;
                }
            }
        }
        EXPECT_EQ(measured, 1) << "cut " << c;
        EXPECT_EQ(prepared, 1) << "cut " << c;
    }
}

// Every global qubit's FINAL state must be claimed by exactly one fragment,
// or the job observable would be applied twice or not at all.
TEST(CutTest, EveryQubitsFinalStateIsClaimedExactlyOnce) {
    const Circuit circuit = two_cluster_circuit();
    const std::optional<CutPlan> maybe = find_cut_plan(circuit, kMaxCuts);
    ASSERT_TRUE(maybe.has_value());
    const CutPlan& plan = maybe.value();

    std::map<QubitId, int> claims;
    for (const CutFragment& fragment : plan.fragments) {
        for (const QubitId local : fragment.final_locals) {
            claims[fragment.local_to_global[local]] += 1;
        }
    }
    for (std::uint32_t q = 0; q < circuit.num_qubits(); ++q) {
        EXPECT_EQ(claims[static_cast<QubitId>(q)], 1) << "global qubit " << q;
    }
}

TEST(CutTest, PreparationGatesArePrependedToTheFragmentCircuit) {
    const Circuit circuit = two_cluster_circuit();
    const std::optional<CutPlan> maybe = find_cut_plan(circuit, kMaxCuts);
    ASSERT_TRUE(maybe.has_value());
    const CutPlan& plan = maybe.value();

    for (const CutFragment& fragment : plan.fragments) {
        if (fragment.prepared.empty()) {
            continue;
        }
        FragmentAssignment assignment;
        assignment.measured_observables.assign(fragment.measured.size(), ir::CutObservable::Z);
        // MinusI needs three Clifford gates, the longest preparation.
        assignment.prepared_states.assign(fragment.prepared.size(), ir::CutPrepState::MinusI);
        const Circuit built = build_fragment_circuit(fragment, assignment);
        EXPECT_EQ(built.operations().size(),
                  fragment.operations.size() + 3 * fragment.prepared.size());

        // Zero needs none, so the difference is visible in the op count.
        assignment.prepared_states.assign(fragment.prepared.size(), ir::CutPrepState::Zero);
        const Circuit bare = build_fragment_circuit(fragment, assignment);
        EXPECT_EQ(bare.operations().size(), fragment.operations.size());
    }
}

// The fragment's Pauli string is its share of the job observable TIMES the
// inserted cut observables (docs/adr/0013 Eq. 2.2).
TEST(CutTest, FragmentTermCombinesTheJobObservableWithTheInsertedCutObservable) {
    const Circuit circuit = two_cluster_circuit();
    const std::optional<CutPlan> maybe = find_cut_plan(circuit, kMaxCuts);
    ASSERT_TRUE(maybe.has_value());
    const CutPlan& plan = maybe.value();

    PauliString z_all;
    z_all.num_qubits = 4;
    z_all.x_words = {0};
    z_all.z_words = {0b1111};

    for (const CutFragment& fragment : plan.fragments) {
        FragmentAssignment assignment;
        // X on every measured cut, so the inserted factor is distinguishable
        // from the job observable's Z factors.
        assignment.measured_observables.assign(fragment.measured.size(), ir::CutObservable::X);
        assignment.prepared_states.assign(fragment.prepared.size(), ir::CutPrepState::Zero);
        const PauliString term = build_fragment_term(fragment, z_all, assignment);

        for (const QubitId local : fragment.final_locals) {
            EXPECT_TRUE(common::get_bit(term.z_words, local))
                << "the job observable's Z is missing on a final qubit";
        }
        for (const CutEndpoint& e : fragment.measured) {
            EXPECT_TRUE(common::get_bit(term.x_words, e.local_qubit))
                << "the inserted cut observable X is missing on a measured wire";
        }
    }
}

// Eq. (4.1). If this drifts, the cost model silently misprices every cut.
TEST(CutTest, DistinctEvaluationCountIsFourToTheMeasuredTimesSixToThePrepared) {
    const Circuit circuit = two_cluster_circuit();
    const std::optional<CutPlan> maybe = find_cut_plan(circuit, kMaxCuts);
    ASSERT_TRUE(maybe.has_value());
    const CutPlan& plan = maybe.value();

    for (const CutFragment& fragment : plan.fragments) {
        std::uint64_t expected = 1;
        for (std::size_t i = 0; i < fragment.measured.size(); ++i) {
            expected *= 4;
        }
        for (std::size_t i = 0; i < fragment.prepared.size(); ++i) {
            expected *= 6;
        }
        EXPECT_EQ(distinct_evaluation_count(fragment), expected);
        EXPECT_EQ(enumerate_assignments(fragment).size(), expected);
    }
}

// Enumeration order is fixed, and every assignment is distinct: the
// reconstruction's summation order depends on it, and so does determinism.
TEST(CutTest, AssignmentEnumerationIsDeterministicAndWithoutDuplicates) {
    const Circuit circuit = two_cluster_circuit();
    const std::optional<CutPlan> maybe = find_cut_plan(circuit, kMaxCuts);
    ASSERT_TRUE(maybe.has_value());
    const CutPlan& plan = maybe.value();

    for (const CutFragment& fragment : plan.fragments) {
        const std::vector<FragmentAssignment> first = enumerate_assignments(fragment);
        const std::vector<FragmentAssignment> second = enumerate_assignments(fragment);
        ASSERT_EQ(first.size(), second.size());
        std::set<std::string> seen;
        for (std::size_t i = 0; i < first.size(); ++i) {
            std::string key;
            for (std::size_t j = 0; j < first[i].measured_observables.size(); ++j) {
                EXPECT_EQ(first[i].measured_observables[j], second[i].measured_observables[j]);
                key += static_cast<char>('a' + static_cast<int>(first[i].measured_observables[j]));
            }
            for (std::size_t j = 0; j < first[i].prepared_states.size(); ++j) {
                EXPECT_EQ(first[i].prepared_states[j], second[i].prepared_states[j]);
                key += static_cast<char>('A' + static_cast<int>(first[i].prepared_states[j]));
            }
            EXPECT_TRUE(seen.insert(key).second) << "duplicate assignment at index " << i;
        }
    }
}

// A branch selects one assignment per fragment, and that mapping must agree
// with the enumeration order, or the reconstruction reads the wrong values.
TEST(CutTest, BranchIndexMapsToTheMatchingEnumeratedAssignment) {
    const Circuit circuit = two_cluster_circuit();
    const std::optional<CutPlan> maybe = find_cut_plan(circuit, kMaxCuts);
    ASSERT_TRUE(maybe.has_value());
    const CutPlan& plan = maybe.value();
    const std::span<const ir::WireCutTerm> terms = ir::wire_cut_terms();

    for (std::size_t branch_term = 0; branch_term < terms.size(); ++branch_term) {
        const std::vector<std::size_t> branch(plan.cuts.size(), branch_term);
        for (const CutFragment& fragment : plan.fragments) {
            const std::size_t index = assignment_index_for_branch(fragment, branch);
            const std::vector<FragmentAssignment> all = enumerate_assignments(fragment);
            ASSERT_LT(index, all.size());
            for (std::size_t j = 0; j < fragment.measured.size(); ++j) {
                EXPECT_EQ(all[index].measured_observables[j], terms[branch_term].observable);
            }
            for (std::size_t j = 0; j < fragment.prepared.size(); ++j) {
                EXPECT_EQ(all[index].prepared_states[j], terms[branch_term].prepared);
            }
        }
    }
}

// Phase 9 reconstructs expectation values only, so a circuit carrying a
// measurement is refused rather than silently mishandled.
TEST(CutTest, CircuitsWithMeasurementsAreRefused) {
    Circuit circuit(2, 2);
    circuit.append(Opcode::H, {0});
    circuit.append(Opcode::CX, {0, 1});
    circuit.append_measure(0, 0);
    EXPECT_FALSE(find_cut_plan(circuit, kMaxCuts).has_value());
}

TEST(CutTest, ACircuitTooSmallToSplitYieldsNoCutPlan) {
    Circuit circuit(1, 0);
    circuit.append(Opcode::H, {0});
    EXPECT_FALSE(find_cut_plan(circuit, kMaxCuts).has_value());
    Circuit empty(2, 0);
    EXPECT_FALSE(find_cut_plan(empty, kMaxCuts).has_value());
}

TEST(CutTest, FindingACutPlanIsDeterministic) {
    const Circuit circuit = two_cluster_circuit();
    const std::optional<CutPlan> maybe_a = find_cut_plan(circuit, kMaxCuts);
    const std::optional<CutPlan> maybe_b = find_cut_plan(circuit, kMaxCuts);
    ASSERT_TRUE(maybe_a.has_value());
    ASSERT_TRUE(maybe_b.has_value());
    const CutPlan& a = maybe_a.value();
    const CutPlan& b = maybe_b.value();
    ASSERT_EQ(a.cuts.size(), b.cuts.size());
    for (std::size_t i = 0; i < a.cuts.size(); ++i) {
        EXPECT_EQ(a.cuts[i].qubit, b.cuts[i].qubit);
        EXPECT_EQ(a.cuts[i].op_index, b.cuts[i].op_index);
    }
    ASSERT_EQ(a.fragments.size(), b.fragments.size());
    for (std::size_t f = 0; f < a.fragments.size(); ++f) {
        EXPECT_EQ(a.fragments[f].local_to_global, b.fragments[f].local_to_global);
    }
}

// NOLINTEND(bugprone-unchecked-optional-access)

}  // namespace
}  // namespace menos::analysis
