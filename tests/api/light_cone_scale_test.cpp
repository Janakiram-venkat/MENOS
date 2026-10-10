// docs/BUILD_PLAN.md Phase 5 acceptance criterion 3: a circuit direct
// dense cannot even represent runs successfully through a light-cone-
// reduced plan and matches the oracle on the reduced circuit.

#include "menos/menos.h"

#include <gtest/gtest.h>

namespace menos {
namespace {

constexpr std::uint32_t kNumQubits = 40;
constexpr double kTheta = 0.7;

// A 40-qubit circuit with one small, connected "interesting" region --
// qubits 19, 20, 21, linked by CX(19,20), H(20), RZ(20, kTheta),
// CX(20,21) -- and 37 other qubits, each touched only by an isolated
// single-qubit gate that never interacts with qubits 19-21 or with each
// other. Because no two-qubit gate ever connects the padding qubits to
// the 19-21 region (by construction, not by the light-cone pass's own
// analysis), the light cone of an observable local to qubit 20 provably
// excludes every padding qubit, independent of trusting
// analysis::compute_light_cone's own correctness (covered separately by
// tests/analysis/light_cone_test.cpp).
Circuit make_forty_qubit_circuit_with_local_region() {
    Circuit circuit(kNumQubits, 0);
    for (QubitId q = 0; q < kNumQubits; ++q) {
        if (q == 19 || q == 20 || q == 21) {
            continue;
        }
        circuit.append(Opcode::H, {q});
    }
    circuit.append(Opcode::CX, {19, 20});
    circuit.append(Opcode::H, {20});
    circuit.append(Opcode::RZ, {20}, {kTheta});
    circuit.append(Opcode::CX, {20, 21});
    return circuit;
}

// The same three-qubit region, built independently as its own 3-qubit
// circuit (qubits 19,20,21 -> local 0,1,2), the oracle this test
// compares against.
Circuit make_independent_reduced_circuit() {
    Circuit circuit(3, 0);
    circuit.append(Opcode::CX, {0, 1});
    circuit.append(Opcode::H, {1});
    circuit.append(Opcode::RZ, {1}, {kTheta});
    circuit.append(Opcode::CX, {1, 2});
    return circuit;
}

// n (a qubit count) and q (which qubit) are not realistically confusable
// at either call site below.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
Observable z_on_qubit(std::uint32_t n, QubitId q) {
    PauliString z;
    z.num_qubits = n;
    z.x_words.assign((n + 63) / 64, 0);
    z.z_words.assign((n + 63) / 64, 0);
    z.z_words[q / 64] |= (std::uint64_t{1} << (q % 64));
    return Observable{{{z, {1.0, 0.0}}}};
}

TEST(LightConeScaleTest,
     FortyQubitCircuitWithLocalObservableMatchesIndependentlyBuiltReducedCircuit) {
    const Circuit full_circuit = make_forty_qubit_circuit_with_local_region();
    const Observable full_observable = z_on_qubit(kNumQubits, 20);

    // A budget far too small for a 40-qubit dense statevector
    // (16 * 2^40 bytes ~ 17.6 TB) but ample for the few-qubit reduced
    // circuit light-cone reduction actually needs to simulate.
    constexpr std::uint64_t kBudgetBytes = 10'000'000;
    const RunOptions full_options{.shots = 0,
                                  .want_statevector = false,
                                  .seed = 0,
                                  .memory_budget_bytes = kBudgetBytes,
                                  .expectation = full_observable};
    const Result full_result = run(full_circuit, full_options);

    // DirectDense, costed against the light-cone-REDUCED circuit, is
    // what actually ran -- but the light-cone summary proves the
    // original 40-qubit circuit could never have been simulated whole
    // within this budget (16 * 2^40 vastly exceeds it), confirming
    // light-cone reduction, not luck, is what made this job feasible.
    EXPECT_EQ(full_result.plan().light_cone.original_num_qubits, kNumQubits);
    EXPECT_LT(full_result.plan().light_cone.reduced_num_qubits, kNumQubits);
    ASSERT_TRUE(full_result.expectation_value().has_value());

    const Circuit reduced_circuit = make_independent_reduced_circuit();
    const Observable reduced_observable = z_on_qubit(3, 1);
    const RunOptions reduced_options{
        .shots = 0, .want_statevector = false, .seed = 0, .expectation = reduced_observable};
    const Result reduced_result = run(reduced_circuit, reduced_options);
    ASSERT_TRUE(reduced_result.expectation_value().has_value());

    // NOLINTBEGIN(bugprone-unchecked-optional-access)
    EXPECT_NEAR(
        full_result.expectation_value()->real(), reduced_result.expectation_value()->real(), 1e-12);
    EXPECT_NEAR(
        full_result.expectation_value()->imag(), reduced_result.expectation_value()->imag(), 1e-12);
    // NOLINTEND(bugprone-unchecked-optional-access)
}

}  // namespace
}  // namespace menos
