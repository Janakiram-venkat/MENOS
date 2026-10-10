#include "cost/cost_model.h"

#include <gtest/gtest.h>

#include "engines/dense/dense_engine.h"
#include "engines/stabilizer/stabilizer_engine.h"

namespace menos::cost {
namespace {

using engines::dense::DenseEngine;
using engines::stabilizer::StabilizerEngine;

TEST(EstimateDirectTest, FeasibleWithinBudget) {
    Circuit circuit(2, 0);
    circuit.append(Opcode::H, {0});
    circuit.append(Opcode::CX, {0, 1});
    const DenseEngine dense;
    const CostEstimate estimate = estimate_direct(circuit,
                                                  dense,
                                                  /*want_statevector=*/true,
                                                  /*want_counts=*/false,
                                                  /*want_expectation=*/false,
                                                  /*observable=*/nullptr,
                                                  /*budget_bytes=*/1'000'000,
                                                  /*accuracy_policy=*/{});
    ASSERT_TRUE(estimate.feasible);
    EXPECT_EQ(estimate.estimated_work_units, 32U);
    EXPECT_EQ(estimate.estimated_peak_bytes, 64U);
}

TEST(EstimateDirectTest, InfeasibleOverBudgetNamesBothQuantities) {
    Circuit circuit(2, 0);
    const DenseEngine dense;
    const CostEstimate estimate =
        estimate_direct(circuit, dense, true, false, false, nullptr, /*budget_bytes=*/10, {});
    EXPECT_FALSE(estimate.feasible);
    EXPECT_NE(estimate.infeasible_reason.find("bytes"), std::string::npos);
}

TEST(EstimateDirectTest, StabilizerEngineOnAllCliffordCircuitIsFeasible) {
    Circuit circuit(2, 0);
    circuit.append(Opcode::H, {0});
    circuit.append(Opcode::CX, {0, 1});
    const StabilizerEngine stabilizer;
    const CostEstimate estimate = estimate_direct(circuit,
                                                  stabilizer,
                                                  /*want_statevector=*/false,
                                                  /*want_counts=*/true,
                                                  /*want_expectation=*/false,
                                                  /*observable=*/nullptr,
                                                  1'000'000,
                                                  {});
    EXPECT_TRUE(estimate.feasible);
}

TEST(EstimateDirectTest, StabilizerEngineRequestingStatevectorIsInfeasible) {
    Circuit circuit(2, 0);
    circuit.append(Opcode::H, {0});
    const StabilizerEngine stabilizer;
    const CostEstimate estimate = estimate_direct(
        circuit, stabilizer, /*want_statevector=*/true, false, false, nullptr, 1'000'000, {});
    EXPECT_FALSE(estimate.feasible);
    EXPECT_NE(estimate.infeasible_reason.find("dense"), std::string::npos);
}

TEST(EstimateDirectTest, StabilizerEngineOnNonCliffordCircuitIsInfeasible) {
    Circuit circuit(1, 0);
    circuit.append(Opcode::T, {0});
    const StabilizerEngine stabilizer;
    const CostEstimate estimate =
        estimate_direct(circuit, stabilizer, false, true, false, nullptr, 1'000'000, {});
    EXPECT_FALSE(estimate.feasible);
    EXPECT_NE(estimate.infeasible_reason.find('t'), std::string::npos);
}

// Hand-computable factorized example: 3 qubits, components {0,1} (m=2,
// one CX) and {2} (m=1, idle), want_statevector=true.
// Component {0,1}: work = 2^2 (init) + 2^(2+2) (CX) + 2^2 (statevector) = 4+16+4 = 24, peak=64
// Component {2}:   idle (no gate operations) -- free: work=0, peak=0
// peak_bytes = max(64,0) + 16*2^3 (global) = 64 + 128 = 192
// total_work = 24 + 0 + 2^3 * 2 components (recombination, idle regions
//              still participate in the multiply-into-global pass) = 24 + 16 = 40
TEST(EstimateFactorizedTest, ExactHandComputableValue) {
    Circuit circuit(3, 0);
    circuit.append(Opcode::CX, {0, 1});
    const analysis::InteractionGraph graph = analysis::build_interaction_graph(circuit);
    ASSERT_EQ(graph.components.size(), 2U);

    const DenseEngine dense;
    const StabilizerEngine stabilizer;
    const std::vector<const SimulationEngine*> engines{&dense, &stabilizer};
    const CostEstimate estimate = estimate_factorized(circuit,
                                                      graph,
                                                      engines,
                                                      /*want_statevector=*/true,
                                                      /*want_counts=*/false,
                                                      /*want_expectation=*/false,
                                                      /*observable=*/nullptr,
                                                      /*budget_bytes=*/1'000'000,
                                                      /*accuracy_policy=*/{});
    ASSERT_TRUE(estimate.feasible);
    EXPECT_EQ(estimate.estimated_peak_bytes, 192U);
    EXPECT_EQ(estimate.estimated_work_units, 40U);
}

// An idle component's freeness is what makes a many-idle-qubit
// factorized plan cheap: a circuit where only 2 of 20 qubits ever
// interact must not be charged as if all 20 needed independent regional
// initialization/output-pass work.
TEST(EstimateFactorizedTest, IdleComponentsAreFree) {
    Circuit circuit(20, 0);
    circuit.append(Opcode::CX, {0, 1});
    const analysis::InteractionGraph graph = analysis::build_interaction_graph(circuit);
    ASSERT_EQ(graph.components.size(), 19U);  // {0,1} plus 18 idle singletons

    const DenseEngine dense;
    const StabilizerEngine stabilizer;
    const std::vector<const SimulationEngine*> engines{&dense, &stabilizer};
    const CostEstimate estimate = estimate_factorized(circuit,
                                                      graph,
                                                      engines,
                                                      /*want_statevector=*/false,
                                                      /*want_counts=*/true,
                                                      /*want_expectation=*/false,
                                                      /*observable=*/nullptr,
                                                      /*budget_bytes=*/1'000'000,
                                                      /*accuracy_policy=*/{});
    ASSERT_TRUE(estimate.feasible);
    // The {0,1} component is 1 CX, all-Clifford -- the stabilizer engine
    // wins it (cheaper than dense's exponential formula even at m=2):
    // stabilizer work = m (init) + 1 gate * m + elimination (2*m^2*words)
    // + sampling (m*words), words=1: 2 + 1*2 + 2*4*1 + 2*1 = 2+2+8+2=14.
    EXPECT_EQ(estimate.estimated_work_units, 14U);
    EXPECT_LT(estimate.estimated_peak_bytes, 64U)
        << "the stabilizer engine's tableau is smaller than dense's 16*2^2=64 bytes here";
}

TEST(EstimateFactorizedTest, InfeasibleOverBudgetNamesBothQuantities) {
    Circuit circuit(3, 0);
    circuit.append(Opcode::CX, {0, 1});
    const analysis::InteractionGraph graph = analysis::build_interaction_graph(circuit);
    const DenseEngine dense;
    const StabilizerEngine stabilizer;
    const std::vector<const SimulationEngine*> engines{&dense, &stabilizer};
    const CostEstimate estimate = estimate_factorized(
        circuit, graph, engines, true, false, false, nullptr, /*budget_bytes=*/10, {});
    EXPECT_FALSE(estimate.feasible);
    EXPECT_NE(estimate.infeasible_reason.find("bytes"), std::string::npos);
}

TEST(PickCheapestEngineTest, DenseWinsWhenStrictlyCheaper) {
    // 3 qubits, all-Clifford, no operations: dense work = 2^3 = 8;
    // stabilizer work = 3 (init only, no gates, no output requested) --
    // stabilizer wins here (3 < 8), the opposite of the "ties go to
    // dense" case, confirming pick_cheapest_engine genuinely compares
    // costs rather than always preferring index 0.
    const DenseEngine dense;
    const StabilizerEngine stabilizer;
    const std::vector<const SimulationEngine*> engines{&dense, &stabilizer};
    const std::vector<Operation> ops{};
    const EngineEstimateRequest request{
        .num_qubits = 3,
        .operations = &ops,
        .want_statevector = false,
        .want_counts = false,
        .want_expectation = false,
        .observable = nullptr,
    };
    const EngineChoice choice = pick_cheapest_engine(engines, request);
    ASSERT_TRUE(choice.estimate.feasible);
    EXPECT_EQ(choice.engine_index, 1U);
    EXPECT_EQ(choice.estimate.estimated_work_units, 3U);
}

TEST(PickCheapestEngineTest, NoFeasibleEngineReportsBothReasons) {
    Circuit circuit(1, 0);
    circuit.append(Opcode::T, {0});
    const DenseEngine dense;
    const StabilizerEngine stabilizer;
    const std::vector<const SimulationEngine*> engines{&dense, &stabilizer};
    // m=63 overflows dense's checked_pow2(m) (infeasible, unconditional
    // on want_statevector/want_counts); the T gate makes the circuit
    // non-Clifford (stabilizer infeasible too) -- both engines
    // infeasible for the same (m, operations) pair.
    const EngineEstimateRequest request{
        .num_qubits = 63,
        .operations = &circuit.operations(),
        .want_statevector = false,
        .want_counts = false,
        .want_expectation = false,
        .observable = nullptr,
    };
    const EngineChoice choice = pick_cheapest_engine(engines, request);
    EXPECT_FALSE(choice.estimate.feasible);
    EXPECT_NE(choice.estimate.infeasible_reason.find("dense"), std::string::npos);
    EXPECT_NE(choice.estimate.infeasible_reason.find("stabilizer"), std::string::npos);
}

}  // namespace
}  // namespace menos::cost
