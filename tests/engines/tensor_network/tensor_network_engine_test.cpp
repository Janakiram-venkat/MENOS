// docs/BUILD_PLAN.md Phase 8 acceptance criteria 1 (closed-network
// expectation matches an independent oracle, through the actual
// SimulationEngine interface this time, not contraction.h's own
// lower-level entry points -- tests/engines/tensor_network/
// contraction_test.cpp already covers those directly) and 3
// (determinism, at the engine level).

#include "engines/tensor_network/tensor_network_engine.h"

#include <gtest/gtest.h>

#include <array>
#include <complex>
#include <vector>

#include "common/error.h"
#include "common/pauli_row.h"
#include "common/rng.h"
#include "engines/dense/apply_matrix.h"
#include "engines/dense/state_vector.h"
#include "ir/gate_table.h"
#include "menos/circuit.h"

namespace menos::engines::tensor_network {
namespace {

using Complex = std::complex<double>;
constexpr double kTolerance = 1e-9;

std::vector<Complex> terms_of(EngineExecutionResult result) {
    // want_expectation is always true at every call site below.
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    return std::move(*result.expectation_terms);
}

TEST(TensorNetworkEngineTest, NameAndCapabilities) {
    const TensorNetworkEngine engine;
    EXPECT_EQ(engine.name(), "tensor_network");
    EXPECT_EQ(engine.engine_id(), Engine::TensorNetwork);
    const EngineCapabilities caps = engine.capabilities();
    EXPECT_FALSE(caps.counts);
    EXPECT_FALSE(caps.statevector);
    EXPECT_TRUE(caps.expectation);
}

TEST(TensorNetworkEngineTest, EstimateRejectsStatevectorRequest) {
    const TensorNetworkEngine engine;
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

TEST(TensorNetworkEngineTest, EstimateRejectsCountsRequest) {
    const TensorNetworkEngine engine;
    const std::vector<Operation> ops{GateOp{Opcode::H, {0}, {}}};
    const EngineEstimateRequest request{
        .num_qubits = 1,
        .operations = &ops,
        .want_statevector = false,
        .want_counts = true,
        .want_expectation = false,
        .observable = nullptr,
    };
    const CostEstimate estimate = engine.estimate(request);
    EXPECT_FALSE(estimate.feasible);
    EXPECT_FALSE(estimate.infeasible_reason.empty());
}

TEST(TensorNetworkEngineTest, EstimateRejectsRequestWithNoOutputWanted) {
    const TensorNetworkEngine engine;
    const std::vector<Operation> ops{GateOp{Opcode::H, {0}, {}}};
    const EngineEstimateRequest request{
        .num_qubits = 1,
        .operations = &ops,
        .want_statevector = false,
        .want_counts = false,
        .want_expectation = false,
        .observable = nullptr,
    };
    const CostEstimate estimate = engine.estimate(request);
    EXPECT_FALSE(estimate.feasible);
    EXPECT_FALSE(estimate.infeasible_reason.empty());
}

TEST(TensorNetworkEngineTest, EstimateIsFeasibleWithPositiveCostsForAnEntanglingCircuit) {
    const TensorNetworkEngine engine;
    const std::vector<Operation> ops{
        GateOp{Opcode::H, {0}, {}},
        GateOp{Opcode::CX, {0, 1}, {}},
    };
    PauliString z0z1;
    z0z1.num_qubits = 2;
    z0z1.x_words = {0};
    z0z1.z_words = {0b11};
    const Observable obs{{{z0z1, {1.0, 0.0}}}};
    const EngineEstimateRequest request{
        .num_qubits = 2,
        .operations = &ops,
        .want_statevector = false,
        .want_counts = false,
        .want_expectation = true,
        .observable = &obs,
    };
    const CostEstimate estimate = engine.estimate(request);
    ASSERT_TRUE(estimate.feasible);
    EXPECT_GT(estimate.estimated_work_units, 0U);
    EXPECT_GT(estimate.estimated_peak_bytes, 0U);
}

// docs/BUILD_PLAN.md Phase 8, resolved with the owner: a budget too
// tight for the min-fill search to even finish must be reported
// honestly -- infeasible (the planner has no other way to read a
// CostEstimate), but worded as "did not fit this budget", never as a
// claim that no valid plan exists at any budget (analysis/treewidth.h's
// own EliminationOrder::exceeded_budget doc comment). This also confirms
// estimate() returns quickly rather than running the full search to
// completion first.
TEST(TensorNetworkEngineTest, TooTightBudgetIsReportedHonestlyNotAsProvenInfeasible) {
    const TensorNetworkEngine engine;
    const std::vector<Operation> ops{
        GateOp{Opcode::H, {0}, {}},
        GateOp{Opcode::CX, {0, 1}, {}},
    };
    PauliString z0z1;
    z0z1.num_qubits = 2;
    z0z1.x_words = {0};
    z0z1.z_words = {0b11};
    const Observable obs{{{z0z1, {1.0, 0.0}}}};
    const EngineEstimateRequest request{
        .num_qubits = 2,
        .operations = &ops,
        .want_statevector = false,
        .want_counts = false,
        .want_expectation = true,
        .observable = &obs,
        .accuracy_policy = {},
        .budget_bytes = 1,  // smaller than even a single rank-1 tensor
    };
    const CostEstimate estimate = engine.estimate(request);
    EXPECT_FALSE(estimate.feasible);
    EXPECT_NE(estimate.infeasible_reason.find("budget"), std::string::npos)
        << estimate.infeasible_reason;
    EXPECT_NE(estimate.infeasible_reason.find("stopped early"), std::string::npos)
        << "must read as \"the search was stopped early\", not as a proof of infeasibility: "
        << estimate.infeasible_reason;
}

// --- Differential test against an independent dense oracle, through the
// full SimulationEngine interface (estimate() then execute()), mirroring
// tests/engines/pauli/pauli_propagation_engine_test.cpp's own structure.

std::vector<Complex> run_on_dense(const Circuit& circuit) {
    dense::StateVector state(static_cast<int>(circuit.num_qubits()), std::uint64_t{1} << 30);
    dense::initialize(state, 0.0);
    for (const Operation& op : circuit.operations()) {
        const auto* gate = std::get_if<GateOp>(&op);
        common::ensure(gate != nullptr, "run_on_dense: random circuit generator only emits GateOp");
        const std::vector<Complex> matrix = ir::generate_matrix(gate->opcode, gate->params);
        dense::apply_matrix(state.amplitudes(), matrix, gate->qubits);
    }
    return std::vector<Complex>(state.amplitudes().begin(), state.amplitudes().end());
}

Complex dense_expectation(const std::vector<Complex>& psi, const PauliString& pauli) {
    std::vector<Complex> p_psi = psi;
    for (std::uint32_t q = 0; q < pauli.num_qubits; ++q) {
        const bool x = common::get_bit(pauli.x_words, q);
        const bool z = common::get_bit(pauli.z_words, q);
        if (!x && !z) {
            continue;
        }
        const std::array<Complex, 4> matrix = common::pauli_matrix_2x2(x, z);
        dense::apply_matrix(p_psi, matrix, std::vector<QubitId>{q});
    }
    Complex result{0.0, 0.0};
    for (std::size_t i = 0; i < psi.size(); ++i) {
        result += std::conj(psi[i]) * p_psi[i];
    }
    return result;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
Circuit random_circuit(std::uint32_t num_qubits, std::uint32_t num_ops, common::Rng& rng) {
    Circuit circuit(num_qubits, 0);
    constexpr std::array<Opcode, 8> kSingleQubit = {
        Opcode::X, Opcode::Y, Opcode::Z, Opcode::H, Opcode::S, Opcode::Sdg, Opcode::T, Opcode::SX};
    constexpr std::array<Opcode, 3> kTwoQubit = {Opcode::CX, Opcode::CZ, Opcode::SWAP};

    for (std::uint32_t i = 0; i < num_ops; ++i) {
        const bool two_qubit = num_qubits >= 2 && rng.next_uniform() < 0.5;
        if (two_qubit) {
            const Opcode opcode = kTwoQubit.at(static_cast<std::size_t>(
                rng.next_uniform() * static_cast<double>(kTwoQubit.size())));
            QubitId a = static_cast<QubitId>(rng.next_uniform() * num_qubits);
            QubitId b = static_cast<QubitId>(rng.next_uniform() * num_qubits);
            if (a == b) {
                b = (a + 1) % num_qubits;
            }
            circuit.append(opcode, {a, b});
        } else {
            const Opcode opcode = kSingleQubit.at(static_cast<std::size_t>(
                rng.next_uniform() * static_cast<double>(kSingleQubit.size())));
            const QubitId q = static_cast<QubitId>(rng.next_uniform() * num_qubits);
            circuit.append(opcode, {q});
        }
    }
    return circuit;
}

PauliString random_pauli(std::uint32_t num_qubits, common::Rng& rng) {
    PauliString pauli{num_qubits, {0}, {0}};
    for (std::uint32_t q = 0; q < num_qubits; ++q) {
        const int kind = static_cast<int>(rng.next_uniform() * 4);  // I, X, Y, Z
        if (kind == 1 || kind == 2) {
            common::set_bit(pauli.x_words, q, true);
        }
        if (kind == 2 || kind == 3) {
            common::set_bit(pauli.z_words, q, true);
        }
    }
    return pauli;
}

EngineExecutionResult run_engine(const TensorNetworkEngine& engine,
                                 const Circuit& circuit,
                                 const Observable& obs) {
    const EngineEstimateRequest est_request{
        .num_qubits = circuit.num_qubits(),
        .operations = &circuit.operations(),
        .want_statevector = false,
        .want_counts = false,
        .want_expectation = true,
        .observable = &obs,
    };
    const CostEstimate estimate = engine.estimate(est_request);
    if (!estimate.feasible) {
        ADD_FAILURE() << "estimate() unexpectedly infeasible: " << estimate.infeasible_reason;
        return {};
    }
    const EngineExecutionRequest request{
        .num_qubits = circuit.num_qubits(),
        .operations = circuit.operations(),
        .want_statevector = false,
        .want_counts = false,
        .want_expectation = true,
        .shots = 0,
        .measure_targets = {},
        .bitstring_width = 0,
        .seed = 0,
        .global_phase = 0.0,
        .budget_bytes = 1'000'000'000,
        .is_sole_region = true,
        .observable = &obs,
    };
    return engine.execute(request);
}

// docs/BUILD_PLAN.md Phase 8: the estimate must BOUND what execution
// actually does, which is a different claim from "the estimator's
// arithmetic is right" (CLAUDE.md's testing rules: "Validating a metric's
// arithmetic is not validating the metric"). The treewidth-exactness suite
// in tests/analysis/treewidth_test.cpp establishes that min-fill reproduces
// known treewidths; it says nothing about whether 16 * 2^width bounds the
// peak intermediate tensor the contraction really reaches. Nothing checked
// that until this test: explain() printed the estimated and actual figures
// side by side, but no assertion related them. estimate() and execute()
// derive their numbers independently -- estimate() from the elimination
// order's own width, execute() from the measured peak leg count of a live
// tensor -- so this is a genuine cross-check, not a tautology.
TEST(TensorNetworkEngineTest, EstimatedPeakBytesBoundTheActualPeakAcrossRandomCircuits) {
    const TensorNetworkEngine engine;
    for (std::uint32_t num_qubits = 2; num_qubits <= 6; ++num_qubits) {
        for (std::uint64_t seed = 0; seed < 6; ++seed) {
            common::Rng rng(seed * 977 + num_qubits);
            const Circuit circuit = random_circuit(num_qubits, /*num_ops=*/3 * num_qubits, rng);
            const PauliString pauli = random_pauli(num_qubits, rng);
            const Observable obs{{{pauli, {1.0, 0.0}}}};

            const EngineEstimateRequest est_request{
                .num_qubits = circuit.num_qubits(),
                .operations = &circuit.operations(),
                .want_statevector = false,
                .want_counts = false,
                .want_expectation = true,
                .observable = &obs,
            };
            const CostEstimate estimate = engine.estimate(est_request);
            ASSERT_TRUE(estimate.feasible) << estimate.infeasible_reason;

            const EngineExecutionResult result = run_engine(engine, circuit, obs);

            EXPECT_LE(result.actual_bytes, estimate.estimated_peak_bytes)
                << "the estimate must be an upper bound on the peak the contraction actually "
                   "reaches, or the planner's own memory-feasibility decision is unsound: "
                   "seed="
                << seed << " num_qubits=" << num_qubits
                << " estimated=" << estimate.estimated_peak_bytes
                << " actual=" << result.actual_bytes;
        }
    }
}

TEST(TensorNetworkEngineTest, RandomCircuitsMatchDenseOracleAcrossQubitCountsAndObservables) {
    const TensorNetworkEngine engine;
    for (std::uint32_t num_qubits = 2; num_qubits <= 5; ++num_qubits) {
        for (std::uint64_t seed = 0; seed < 4; ++seed) {
            common::Rng rng(seed * 100 + num_qubits);
            const Circuit circuit = random_circuit(num_qubits, /*num_ops=*/2 * num_qubits, rng);
            const PauliString pauli = random_pauli(num_qubits, rng);
            const Observable obs{{{pauli, {1.0, 0.0}}}};

            const std::vector<Complex> psi = run_on_dense(circuit);
            const Complex expected = dense_expectation(psi, pauli);
            const Complex actual = terms_of(run_engine(engine, circuit, obs)).at(0);

            EXPECT_NEAR(actual.real(), expected.real(), kTolerance)
                << "seed=" << seed << " num_qubits=" << num_qubits;
            EXPECT_NEAR(actual.imag(), expected.imag(), kTolerance)
                << "seed=" << seed << " num_qubits=" << num_qubits;
        }
    }
}

// Acceptance criterion 3 (determinism), at the engine level: repeated
// execute() calls on the same request give bit-identical expectation
// terms and the same actual_bytes.
TEST(TensorNetworkEngineTest, RepeatedExecuteIsBitIdentical) {
    const TensorNetworkEngine engine;
    Circuit circuit(3, 0);
    circuit.append(Opcode::H, {0});
    circuit.append(Opcode::CX, {0, 1});
    circuit.append(Opcode::CX, {1, 2});
    PauliString term;
    term.num_qubits = 3;
    term.x_words = {0};
    term.z_words = {0b101};
    const Observable obs{{{term, {1.0, 0.0}}}};

    const EngineExecutionResult first = run_engine(engine, circuit, obs);
    const EngineExecutionResult second = run_engine(engine, circuit, obs);

    ASSERT_TRUE(first.expectation_terms.has_value());
    ASSERT_TRUE(second.expectation_terms.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    EXPECT_EQ(*first.expectation_terms, *second.expectation_terms);
    EXPECT_EQ(first.actual_bytes, second.actual_bytes);
}

}  // namespace
}  // namespace menos::engines::tensor_network
