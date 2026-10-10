#include "engines/mps/mps_engine.h"

#include <gtest/gtest.h>

#include <cmath>
#include <complex>
#include <type_traits>
#include <variant>
#include <vector>

#include "common/pauli_row.h"
#include "engines/dense/apply_matrix.h"
#include "engines/dense/state_vector.h"
#include "ir/gate_table.h"
#include "menos/circuit.h"

namespace menos::engines::mps {
namespace {

using Complex = std::complex<double>;

TEST(MPSEngineTest, NameAndCapabilities) {
    const MPSEngine engine;
    EXPECT_EQ(engine.name(), "mps");
    EXPECT_EQ(engine.engine_id(), Engine::MPS);
    const EngineCapabilities caps = engine.capabilities();
    EXPECT_TRUE(caps.counts);
    EXPECT_FALSE(caps.statevector);
    EXPECT_TRUE(caps.expectation);
}

TEST(MPSEngineTest, EstimateRejectsStatevectorRequest) {
    const MPSEngine engine;
    const std::vector<Operation> ops{GateOp{Opcode::H, {0}, {}}};
    const EngineEstimateRequest request{
        .num_qubits = 1,
        .operations = &ops,
        .want_statevector = true,
        .want_counts = false,
        .want_expectation = false,
        .observable = nullptr,
    };
    const CostEstimate estimate = engine.estimate(request);
    EXPECT_FALSE(estimate.feasible);
    EXPECT_FALSE(estimate.infeasible_reason.empty());
}

TEST(MPSEngineTest, EstimateIsFeasibleWithPositiveCostsForAnEntanglingCircuit) {
    const MPSEngine engine;
    const std::vector<Operation> ops{
        GateOp{Opcode::H, {0}, {}},
        GateOp{Opcode::CX, {0, 1}, {}},
    };
    const EngineEstimateRequest request{
        .num_qubits = 2,
        .operations = &ops,
        .want_statevector = false,
        .want_counts = true,
        .want_expectation = false,
        .observable = nullptr,
    };
    const CostEstimate estimate = engine.estimate(request);
    ASSERT_TRUE(estimate.feasible);
    EXPECT_GT(estimate.estimated_work_units, 0U);
    EXPECT_GT(estimate.estimated_peak_bytes, 0U);
}

// A tight Controlled max_bond_dimension caps the rigorous bound used
// for peak_bytes -- the estimate must shrink relative to Exact for a
// circuit whose true bound exceeds the cap.
TEST(MPSEngineTest, ControlledMaxBondDimensionLowersThePeakByteEstimate) {
    const MPSEngine engine;
    // A 4-qubit GHZ chain has Schmidt rank 2 at every cut.
    const std::vector<Operation> ops{
        GateOp{Opcode::H, {0}, {}},
        GateOp{Opcode::CX, {0, 1}, {}},
        GateOp{Opcode::CX, {1, 2}, {}},
        GateOp{Opcode::CX, {2, 3}, {}},
    };
    const EngineEstimateRequest exact_request{
        .num_qubits = 4,
        .operations = &ops,
        .want_statevector = false,
        .want_counts = false,
        .want_expectation = false,
        .observable = nullptr,
    };
    AccuracyPolicy controlled;
    controlled.kind = AccuracyPolicyKind::Controlled;
    controlled.max_bond_dimension = 1;
    const EngineEstimateRequest controlled_request{
        .num_qubits = 4,
        .operations = &ops,
        .want_statevector = false,
        .want_counts = false,
        .want_expectation = false,
        .observable = nullptr,
        .accuracy_policy = controlled,
    };

    const CostEstimate exact_estimate = engine.estimate(exact_request);
    const CostEstimate controlled_estimate = engine.estimate(controlled_request);
    ASSERT_TRUE(exact_estimate.feasible);
    ASSERT_TRUE(controlled_estimate.feasible);
    EXPECT_LT(controlled_estimate.estimated_peak_bytes, exact_estimate.estimated_peak_bytes);
}

TEST(MPSEngineTest, ExecuteBellCircuitProducesCorrelatedCounts) {
    const MPSEngine engine;
    const std::vector<Operation> ops{
        GateOp{Opcode::H, {0}, {}},
        GateOp{Opcode::CX, {0, 1}, {}},
    };
    const std::vector<std::pair<QubitId, ClbitId>> targets{{0, 0}, {1, 1}};
    const EngineExecutionRequest request{
        .num_qubits = 2,
        .operations = ops,
        .want_statevector = false,
        .want_counts = true,
        .want_expectation = false,
        .shots = 1000,
        .measure_targets = targets,
        .bitstring_width = 2,
        .seed = 42,
        .global_phase = 0.0,
        .budget_bytes = 1'000'000,
        .is_sole_region = true,
        .observable = nullptr,
    };
    const EngineExecutionResult result = engine.execute(request);
    EXPECT_FALSE(result.statevector.has_value());
    ASSERT_TRUE(result.counts.has_value());
    std::uint64_t total = 0;
    for (const auto& [key, count] : *result.counts) {  // NOLINT(bugprone-unchecked-optional-access)
        EXPECT_TRUE(key.test(0) == key.test(1)) << "Bell state should only measure |00> or |11>";
        total += count;
    }
    EXPECT_EQ(total, 1000U);
    EXPECT_FALSE(result.accumulated_error_bound.has_value()) << "Exact policy must never truncate";
}

TEST(MPSEngineTest, MultiRegionRequestReturnsCountsSamplerNotHistogram) {
    const MPSEngine engine;
    const std::vector<Operation> ops{GateOp{Opcode::H, {0}, {}}};
    const std::vector<std::pair<QubitId, ClbitId>> targets{{0, 0}};
    const EngineExecutionRequest request{
        .num_qubits = 1,
        .operations = ops,
        .want_statevector = false,
        .want_counts = true,
        .want_expectation = false,
        .shots = 100,
        .measure_targets = targets,
        .bitstring_width = 1,
        .seed = 1,
        .global_phase = 0.0,
        .budget_bytes = 1'000,
        .is_sole_region = false,
        .observable = nullptr,
    };
    const EngineExecutionResult result = engine.execute(request);
    EXPECT_FALSE(result.counts.has_value());
    EXPECT_NE(result.counts_sampler, nullptr);
}

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

std::complex<double> dense_expectation(const std::vector<Complex>& psi, const PauliString& pauli) {
    std::vector<Complex> p_psi = psi;
    const std::uint32_t num_qubits = pauli.num_qubits;
    for (std::uint32_t q = 0; q < num_qubits; ++q) {
        const bool x = common::get_bit(pauli.x_words, q);
        const bool z = common::get_bit(pauli.z_words, q);
        std::vector<Complex> matrix;
        if (!x && !z) {
            continue;
        } else if (x && !z) {
            matrix = {Complex{0, 0}, Complex{1, 0}, Complex{1, 0}, Complex{0, 0}};
        } else if (!x && z) {
            matrix = {Complex{1, 0}, Complex{0, 0}, Complex{0, 0}, Complex{-1, 0}};
        } else {
            matrix = {Complex{0, 0}, Complex{0, -1}, Complex{0, 1}, Complex{0, 0}};
        }
        dense::apply_matrix(p_psi, matrix, std::vector<QubitId>{q});
    }
    Complex result{0.0, 0.0};
    for (std::size_t i = 0; i < psi.size(); ++i) {
        result += std::conj(psi[i]) * p_psi[i];
    }
    return result;
}

TEST(MPSEngineTest, ExecuteExpectationMatchesDenseOracle) {
    const MPSEngine engine;
    Circuit circuit(3, 0);
    circuit.append(Opcode::H, {0});
    circuit.append(Opcode::CX, {0, 1});
    circuit.append(Opcode::RY, {2}, {0.6});
    circuit.append(Opcode::CX, {1, 2});
    const std::vector<Complex> dense_state = run_on_dense(circuit);

    PauliString pauli;
    pauli.num_qubits = 3;
    pauli.x_words.assign(1, 0);
    pauli.z_words.assign(1, 0);
    common::set_bit(pauli.x_words, 0, true);  // X
    common::set_bit(pauli.z_words, 1, true);  // Z
    common::set_bit(pauli.x_words, 2, true);  // Y
    common::set_bit(pauli.z_words, 2, true);
    const Complex expected = dense_expectation(dense_state, pauli);

    Observable observable;
    observable.terms.emplace_back(pauli, Complex{1.0, 0.0});
    const EngineExecutionRequest request{
        .num_qubits = 3,
        .operations = circuit.operations(),
        .want_statevector = false,
        .want_counts = false,
        .want_expectation = true,
        .shots = 0,
        .measure_targets = {},
        .bitstring_width = 0,
        .seed = 0,
        .global_phase = 0.0,
        .budget_bytes = 1'000'000,
        .is_sole_region = true,
        .observable = &observable,
    };
    const EngineExecutionResult result = engine.execute(request);
    ASSERT_TRUE(result.expectation_terms.has_value());
    // ASSERT_TRUE above already guarantees this; clang-tidy can't see
    // through the gtest macro.
    ASSERT_EQ(result.expectation_terms->size(), 1U);  // NOLINT(bugprone-unchecked-optional-access)
    // NOLINTBEGIN(bugprone-unchecked-optional-access)
    EXPECT_NEAR((*result.expectation_terms)[0].real(), expected.real(), 1e-9);
    EXPECT_NEAR((*result.expectation_terms)[0].imag(), expected.imag(), 1e-9);
    // NOLINTEND(bugprone-unchecked-optional-access)
}

// A tight Controlled cap on a GHZ chain forces truncation: the run must
// report an accumulated_error_bound, and it must be a finite,
// non-negative number (the specific value's relationship to the true
// error is exercised in apply_gate_test.cpp and this bound's own
// derivation, not re-verified here).
TEST(MPSEngineTest, ControlledExecutionWithTightCapReportsAnAccumulatedErrorBound) {
    const MPSEngine engine;
    const std::vector<Operation> ops{
        GateOp{Opcode::H, {0}, {}},
        GateOp{Opcode::CX, {0, 1}, {}},
        GateOp{Opcode::CX, {1, 2}, {}},
        GateOp{Opcode::CX, {2, 3}, {}},
    };
    AccuracyPolicy controlled;
    controlled.kind = AccuracyPolicyKind::Controlled;
    controlled.max_bond_dimension = 1;
    const EngineExecutionRequest request{
        .num_qubits = 4,
        .operations = ops,
        .want_statevector = false,
        .want_counts = true,
        .want_expectation = false,
        .shots = 10,
        .measure_targets = {},
        .bitstring_width = 0,
        .seed = 3,
        .global_phase = 0.0,
        .budget_bytes = 1'000'000,
        .is_sole_region = true,
        .observable = nullptr,
        .accuracy_policy = controlled,
    };
    const EngineExecutionResult result = engine.execute(request);
    ASSERT_TRUE(result.accumulated_error_bound.has_value());
    // ASSERT_TRUE above already guarantees this; clang-tidy can't see
    // through the gtest macro.
    EXPECT_GE(*result.accumulated_error_bound, 0.0);  // NOLINT(bugprone-unchecked-optional-access)
    EXPECT_TRUE(std::isfinite(
        *result.accumulated_error_bound));  // NOLINT(bugprone-unchecked-optional-access)
}

}  // namespace
}  // namespace menos::engines::mps
