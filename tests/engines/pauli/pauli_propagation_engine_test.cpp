#include "engines/pauli/pauli_propagation_engine.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <complex>
#include <random>
#include <vector>

#include "engines/dense/dense_engine.h"
#include "ir/gate_table.h"

namespace menos::engines::pauli {
namespace {

using engines::dense::DenseEngine;
using MeasureTarget = std::pair<QubitId, ClbitId>;

// sum over terms of coefficient * raw <psi|P|psi> -- what
// runtime::combine_expectation does for a single (sole) region, matched
// here directly since these tests call SimulationEngine::execute()
// without going through the runtime.
std::complex<double> combined(const Observable& obs,
                              const std::vector<std::complex<double>>& terms) {
    std::complex<double> total{0.0, 0.0};
    for (std::size_t t = 0; t < obs.terms.size(); ++t) {
        total += obs.terms[t].second * terms[t];
    }
    return total;
}

// want_expectation is always true at every call site below, so
// expectation_terms is always populated; not provable to clang-tidy's
// local static analysis across a multi-statement call chain.
std::vector<std::complex<double>> terms_of(EngineExecutionResult result) {
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    return std::move(*result.expectation_terms);
}

PauliString one_qubit_pauli(bool x_bit, bool z_bit) {
    PauliString p;
    p.num_qubits = 1;
    p.x_words = {x_bit ? std::uint64_t{1} : std::uint64_t{0}};
    p.z_words = {z_bit ? std::uint64_t{1} : std::uint64_t{0}};
    return p;
}

// --- Direct 2x2 matrix computation, independent of PauliRow/apply_*
// entirely, used to verify the engine's output against the actual
// installed gate matrices (docs/adr/0007: "verify the convention
// carefully... tested directly against the actual matrix
// implementation being used"). ---

using Matrix2 = std::array<std::complex<double>, 4>;  // row-major, 2x2

Matrix2 identity2() {
    return {1.0, 0.0, 0.0, 1.0};
}

Matrix2 matmul2(const Matrix2& a, const Matrix2& b) {
    return {
        a[0] * b[0] + a[1] * b[2],
        a[0] * b[1] + a[1] * b[3],
        a[2] * b[0] + a[3] * b[2],
        a[2] * b[1] + a[3] * b[3],
    };
}

Matrix2 dagger2(const Matrix2& m) {
    return {std::conj(m[0]), std::conj(m[2]), std::conj(m[1]), std::conj(m[3])};
}

// U_total for a sequence of single-qubit gates applied in program order:
// state = G_last(...(G_first(psi))...), so U_total = G_last * ... *
// G_first (matrices compose right-to-left as they're applied).
Matrix2 circuit_unitary(const std::vector<GateOp>& gates) {
    Matrix2 total = identity2();
    for (const GateOp& g : gates) {
        const std::vector<std::complex<double>> m = ir::generate_matrix(g.opcode, g.params);
        const Matrix2 gm{m[0], m[1], m[2], m[3]};
        total = matmul2(gm, total);
    }
    return total;
}

// <0|U^dagger * observable * U|0> = (U^dagger * observable * U)[0][0].
std::complex<double> direct_expectation(const std::vector<GateOp>& gates,
                                        const Matrix2& observable) {
    const Matrix2 u = circuit_unitary(gates);
    const Matrix2 result = matmul2(matmul2(dagger2(u), observable), u);
    return result[0];
}

constexpr Matrix2 kX{0.0, 1.0, 1.0, 0.0};
constexpr Matrix2 kY{0.0, {0.0, -1.0}, {0.0, 1.0}, 0.0};
constexpr Matrix2 kZ{1.0, 0.0, 0.0, -1.0};

std::complex<double> engine_expectation_one_qubit(const std::vector<GateOp>& gates,
                                                  const PauliString& observable) {
    const PauliPropagationEngine engine;
    std::vector<Operation> ops(gates.begin(), gates.end());
    const Observable obs{{{observable, {1.0, 0.0}}}};
    const EngineEstimateRequest est_request{
        .num_qubits = 1,
        .operations = &ops,
        .want_statevector = false,
        .want_counts = false,
        .want_expectation = true,
        .observable = &obs,
    };
    const CostEstimate estimate = engine.estimate(est_request);
    if (!estimate.feasible) {
        ADD_FAILURE() << "estimate() unexpectedly infeasible: " << estimate.infeasible_reason;
        return {0.0, 0.0};
    }
    const EngineExecutionRequest request{
        .num_qubits = 1,
        .operations = ops,
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
        .observable = &obs,
    };
    return combined(obs, terms_of(engine.execute(request)));
}

TEST(PauliPropagationEngineTest, NameAndCapabilities) {
    const PauliPropagationEngine engine;
    EXPECT_EQ(engine.name(), "pauli");
    EXPECT_EQ(engine.engine_id(), Engine::Pauli);
    const EngineCapabilities caps = engine.capabilities();
    EXPECT_FALSE(caps.counts);
    EXPECT_FALSE(caps.statevector);
    EXPECT_TRUE(caps.expectation);
}

TEST(PauliPropagationEngineTest, StatevectorRequestIsInfeasible) {
    const PauliPropagationEngine engine;
    const std::vector<Operation> ops{};
    const PauliString z = one_qubit_pauli(false, true);
    const Observable obs{{{z, {1.0, 0.0}}}};
    const EngineEstimateRequest request{
        .num_qubits = 1,
        .operations = &ops,
        .want_statevector = true,
        .want_counts = false,
        .want_expectation = false,
        .observable = &obs,
    };
    const CostEstimate estimate = engine.estimate(request);
    EXPECT_FALSE(estimate.feasible);
    EXPECT_NE(estimate.infeasible_reason.find("statevector"), std::string::npos);
}

TEST(PauliPropagationEngineTest, CountsRequestIsInfeasible) {
    const PauliPropagationEngine engine;
    const std::vector<Operation> ops{};
    const PauliString z = one_qubit_pauli(false, true);
    const Observable obs{{{z, {1.0, 0.0}}}};
    const EngineEstimateRequest request{
        .num_qubits = 1,
        .operations = &ops,
        .want_statevector = false,
        .want_counts = true,
        .want_expectation = false,
        .observable = &obs,
    };
    const CostEstimate estimate = engine.estimate(request);
    EXPECT_FALSE(estimate.feasible);
    EXPECT_NE(estimate.infeasible_reason.find("counts"), std::string::npos);
}

TEST(PauliPropagationEngineTest, UnsupportedGateIsInfeasibleNamingIndexAndGate) {
    const PauliPropagationEngine engine;
    const std::vector<Operation> ops{
        GateOp{Opcode::H, {0}, {}},
        GateOp{Opcode::T, {0}, {}},
    };
    const PauliString z = one_qubit_pauli(false, true);
    const Observable obs{{{z, {1.0, 0.0}}}};
    const EngineEstimateRequest request{
        .num_qubits = 1,
        .operations = &ops,
        .want_statevector = false,
        .want_counts = false,
        .want_expectation = true,
        .observable = &obs,
    };
    const CostEstimate estimate = engine.estimate(request);
    EXPECT_FALSE(estimate.feasible);
    EXPECT_NE(estimate.infeasible_reason.find("operation 1"), std::string::npos);
    EXPECT_NE(estimate.infeasible_reason.find('t'), std::string::npos);
}

TEST(PauliPropagationEngineTest, TermCountBoundExceededIsInfeasibleBeforePropagationStarts) {
    const PauliPropagationEngine engine;
    // 3^h overflows uint64_t well before h=60 (3^40 ~ 1.2e19 is already
    // close to uint64_t's ~1.8e19 ceiling); this must be refused by
    // estimate(), never truncated (CLAUDE.md: "never silently
    // approximate").
    constexpr int kUGateCount = 60;
    std::vector<Operation> ops;
    ops.reserve(kUGateCount);
    for (int i = 0; i < kUGateCount; ++i) {
        ops.push_back(GateOp{Opcode::U, {0}, {1.1, 0.7, -0.4}});
    }
    const PauliString z = one_qubit_pauli(false, true);
    const Observable obs{{{z, {1.0, 0.0}}}};
    const EngineEstimateRequest request{
        .num_qubits = 1,
        .operations = &ops,
        .want_statevector = false,
        .want_counts = false,
        .want_expectation = true,
        .observable = &obs,
    };
    const CostEstimate estimate = engine.estimate(request);
    EXPECT_FALSE(estimate.feasible);
    EXPECT_FALSE(estimate.infeasible_reason.empty());
}

TEST(PauliPropagationEngineTest, ModestCircuitEstimateIsFeasible) {
    const PauliPropagationEngine engine;
    std::vector<Operation> ops{
        GateOp{Opcode::H, {0}, {}},
        GateOp{Opcode::U, {0}, {1.1, 0.7, -0.4}},
        GateOp{Opcode::RZ, {0}, {0.3}},
    };
    const PauliString z = one_qubit_pauli(false, true);
    const Observable obs{{{z, {1.0, 0.0}}}};
    const EngineEstimateRequest request{
        .num_qubits = 1,
        .operations = &ops,
        .want_statevector = false,
        .want_counts = false,
        .want_expectation = true,
        .observable = &obs,
    };
    const CostEstimate estimate = engine.estimate(request);
    ASSERT_TRUE(estimate.feasible);
    // 1 starting term * 3 (one U) * 2 (one RZ) = at most 6 final terms.
    EXPECT_LE(estimate.estimated_work_units, 6U * ops.size());
}

// --- Direct-matrix-conjugation checks for U at non-special parameter
// values (docs/adr/0007's requirement: "verify that conjugating X, Y,
// and Z produces coefficients... that agree with direct matrix
// conjugation"). theta/phi/lambda are chosen so every trig factor in
// the derived a/b/c formulas is nonzero -- no special-case collapse. ---

TEST(PauliPropagationEngineTest, USingleQubitXObservableMatchesDirectMatrixComputation) {
    const std::vector<GateOp> gates{GateOp{Opcode::U, {0}, {1.1, 0.7, -0.4}}};
    const PauliString x = one_qubit_pauli(true, false);
    const std::complex<double> expected = direct_expectation(gates, kX);
    const std::complex<double> actual = engine_expectation_one_qubit(gates, x);
    EXPECT_NEAR(actual.real(), expected.real(), 1e-12);
    EXPECT_NEAR(actual.imag(), expected.imag(), 1e-12);
}

TEST(PauliPropagationEngineTest, USingleQubitYObservableMatchesDirectMatrixComputation) {
    const std::vector<GateOp> gates{GateOp{Opcode::U, {0}, {1.1, 0.7, -0.4}}};
    const PauliString y = one_qubit_pauli(true, true);
    const std::complex<double> expected = direct_expectation(gates, kY);
    const std::complex<double> actual = engine_expectation_one_qubit(gates, y);
    EXPECT_NEAR(actual.real(), expected.real(), 1e-12);
    EXPECT_NEAR(actual.imag(), expected.imag(), 1e-12);
}

TEST(PauliPropagationEngineTest, USingleQubitZObservableMatchesDirectMatrixComputation) {
    const std::vector<GateOp> gates{GateOp{Opcode::U, {0}, {1.1, 0.7, -0.4}}};
    const PauliString z = one_qubit_pauli(false, true);
    const std::complex<double> expected = direct_expectation(gates, kZ);
    const std::complex<double> actual = engine_expectation_one_qubit(gates, z);
    EXPECT_NEAR(actual.real(), expected.real(), 1e-12);
    EXPECT_NEAR(actual.imag(), expected.imag(), 1e-12);
}

// --- Special-case reductions: U collapses to a single-axis rotation
// when its parameters correspond to a rotation about one axis. These
// are confirmations only, per the owner's explicit instruction: "Do not
// use those special cases to justify a general 2-term bound." ---

TEST(PauliPropagationEngineTest, USpecialCaseThetaOnlyMatchesRyDirectly) {
    // U(theta, 0, 0) == RY(theta) exactly (docs/adr/0007).
    const std::vector<GateOp> u_gates{GateOp{Opcode::U, {0}, {0.83, 0.0, 0.0}}};
    const std::vector<GateOp> ry_gates{GateOp{Opcode::RY, {0}, {0.83}}};
    const PauliString x = one_qubit_pauli(true, false);
    const std::complex<double> via_u = engine_expectation_one_qubit(u_gates, x);
    const std::complex<double> via_ry = engine_expectation_one_qubit(ry_gates, x);
    EXPECT_NEAR(via_u.real(), via_ry.real(), 1e-12);
    EXPECT_NEAR(via_u.imag(), via_ry.imag(), 1e-12);
    // Cross-check against the direct matrix computation too.
    const std::complex<double> expected = direct_expectation(u_gates, kX);
    EXPECT_NEAR(via_u.real(), expected.real(), 1e-12);
    EXPECT_NEAR(via_u.imag(), expected.imag(), 1e-12);
}

TEST(PauliPropagationEngineTest, USpecialCasePhiLambdaOnlyMatchesRzUpToGlobalPhase) {
    // U(0, phi, lambda) == e^{i(phi+lambda)/2} * RZ(phi+lambda); the
    // global phase cancels under conjugation, so expectation values
    // must match RZ(phi+lambda) exactly.
    const std::vector<GateOp> u_gates{GateOp{Opcode::U, {0}, {0.0, 0.5, 0.9}}};
    const std::vector<GateOp> rz_gates{GateOp{Opcode::RZ, {0}, {1.4}}};
    const PauliString x = one_qubit_pauli(true, false);
    const std::complex<double> via_u = engine_expectation_one_qubit(u_gates, x);
    const std::complex<double> via_rz = engine_expectation_one_qubit(rz_gates, x);
    EXPECT_NEAR(via_u.real(), via_rz.real(), 1e-12);
    EXPECT_NEAR(via_u.imag(), via_rz.imag(), 1e-12);
}

// --- S/Sdg and SX/SXdg backward-direction-swap pinning test
// (docs/adr/0007: backward through a circuit S gate must apply Sdg's
// rule, and vice versa). A pure diagonal gate (S, Sdg, RZ, P) always
// fixes Z exactly with no sign ambiguity, so this circuit interleaves S
// with H specifically to make the S<->Sdg choice sign-visible in the
// final |0>-expectation. ---

TEST(PauliPropagationEngineTest, HThenSThenHMatchesDirectMatrixComputation) {
    // Hand-derived (docs/adr/0007): backward through H (self-adjoint,
    // unchanged), then S (must call apply_sdg), then H conjugates Y into
    // exactly -Z: swapping in apply_s at the middle step instead would
    // give +Z, the opposite sign -- this circuit is sign-discriminating
    // by construction, not merely nonzero by chance. A trailing RZ(0)
    // (matrix_rz(0) = diag(1,1) = I exactly) keeps this circuit eligible
    // for the engine's own estimate() despite being otherwise fully
    // Clifford (the Clifford-region routing rule in estimate() is a
    // planner-facing eligibility rule, not something this isolated
    // engine-level test needs to route around any other way) without
    // changing the conjugation being tested at all.
    const std::vector<GateOp> gates{
        GateOp{Opcode::H, {0}, {}},
        GateOp{Opcode::S, {0}, {}},
        GateOp{Opcode::H, {0}, {}},
        GateOp{Opcode::RZ, {0}, {0.0}},
    };
    const PauliString y = one_qubit_pauli(true, true);
    const std::complex<double> expected = direct_expectation(gates, kY);
    const std::complex<double> actual = engine_expectation_one_qubit(gates, y);
    ASSERT_GT(std::abs(expected), 1e-6) << "test circuit must give a nonzero, sign-sensitive "
                                           "expectation to actually pin the S/Sdg direction";
    EXPECT_NEAR(actual.real(), expected.real(), 1e-12);
    EXPECT_NEAR(actual.imag(), expected.imag(), 1e-12);
}

TEST(PauliPropagationEngineTest, SxThenHThenSxThenHMatchesDirectMatrixComputation) {
    // A trailing RZ(0) is an exact identity, added for the same reason as
    // HThenSThenHMatchesDirectMatrixComputation above.
    const std::vector<GateOp> gates{
        GateOp{Opcode::SX, {0}, {}},
        GateOp{Opcode::H, {0}, {}},
        GateOp{Opcode::SX, {0}, {}},
        GateOp{Opcode::H, {0}, {}},
        GateOp{Opcode::RZ, {0}, {0.0}},
    };
    const PauliString x = one_qubit_pauli(true, false);
    const std::complex<double> expected = direct_expectation(gates, kX);
    const std::complex<double> actual = engine_expectation_one_qubit(gates, x);
    ASSERT_GT(std::abs(expected), 1e-6) << "test circuit must give a nonzero, sign-sensitive "
                                           "expectation to actually pin the SX/SXdg direction";
    EXPECT_NEAR(actual.real(), expected.real(), 1e-12);
    EXPECT_NEAR(actual.imag(), expected.imag(), 1e-12);
}

// --- Genuine 3x3=9-way term growth: two U gates on two DIFFERENT,
// non-interacting qubits, each individually branching into 3 terms
// under a generic X observable, so their branchings multiply instead of
// collapsing the way two U's composed on the SAME qubit would (owner's
// explicit requirement: "a >=2-U-gate circuit forcing genuine 3x3=9-way
// term growth"). Checked against the dense oracle, not hand-derived. ---

TEST(PauliPropagationEngineDifferentialTest, TwoIndependentUGatesMatchesDenseOracleOnXxX) {
    Circuit circuit(2, 0);
    circuit.append(Opcode::U, {0}, {1.1, 0.7, -0.4});
    circuit.append(Opcode::U, {1}, {0.5, -1.3, 2.1});

    PauliString xx;
    xx.num_qubits = 2;
    xx.x_words = {0b11};
    xx.z_words = {0b00};
    const Observable obs{{{xx, {1.0, 0.0}}}};

    const PauliPropagationEngine pauli_engine;
    const EngineEstimateRequest est_request{
        .num_qubits = 2,
        .operations = &circuit.operations(),
        .want_statevector = false,
        .want_counts = false,
        .want_expectation = true,
        .observable = &obs,
    };
    const CostEstimate estimate = pauli_engine.estimate(est_request);
    ASSERT_TRUE(estimate.feasible);
    EXPECT_GE(estimate.estimated_work_units, 9U)
        << "two independent U gates must be costed for the full 3x3=9-way worst case";

    const EngineExecutionRequest pauli_request{
        .num_qubits = 2,
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
        .observable = &obs,
    };
    const std::complex<double> pauli_value =
        combined(obs, terms_of(pauli_engine.execute(pauli_request)));

    const DenseEngine dense_engine;
    const EngineExecutionRequest dense_request{
        .num_qubits = 2,
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
        .observable = &obs,
    };
    const std::complex<double> dense_value =
        combined(obs, terms_of(dense_engine.execute(dense_request)));

    EXPECT_NEAR(pauli_value.real(), dense_value.real(), 1e-10);
    EXPECT_NEAR(pauli_value.imag(), dense_value.imag(), 1e-10);
}

// --- General random differential test against the dense oracle
// (docs/BUILD_PLAN.md Phase 5 acceptance criterion 5): Clifford gates
// plus RZ/RX/RY/P/U (P(pi/4) standing in for T, exactly, since T =
// P(pi/4) as matrices), random Pauli observables. ---

std::uint32_t random_below(std::mt19937_64& rng, std::uint32_t bound) {
    return static_cast<std::uint32_t>(rng() % bound);
}

double random_angle(std::mt19937_64& rng) {
    std::uniform_real_distribution<double> dist(-3.0, 3.0);
    return dist(rng);
}

// seed (an arbitrary 64-bit RNG seed) and n (a qubit count, 1-4 in this
// test) are not realistically confusable at any call site.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
Circuit make_random_propagation_circuit(std::uint64_t seed, std::uint32_t n) {
    std::mt19937_64 rng(seed);
    Circuit circuit(n, 0);

    const std::array<Opcode, 8> clifford_1q{Opcode::X,
                                            Opcode::Y,
                                            Opcode::Z,
                                            Opcode::H,
                                            Opcode::S,
                                            Opcode::Sdg,
                                            Opcode::SX,
                                            Opcode::SXdg};
    const std::array<Opcode, 4> rotation_1q{Opcode::RX, Opcode::RY, Opcode::RZ, Opcode::P};
    const std::array<Opcode, 2> clifford_2q{Opcode::CX, Opcode::CZ};

    const std::uint32_t num_gates = 4 + random_below(rng, 6);
    for (std::uint32_t i = 0; i < num_gates; ++i) {
        const std::uint32_t choice = random_below(rng, 4);
        if (choice == 0 && n >= 2) {
            const QubitId a = random_below(rng, n);
            QubitId b = random_below(rng, n);
            while (b == a) {
                b = random_below(rng, n);
            }
            circuit.append(clifford_2q[random_below(rng, 2)], {a, b});
        } else if (choice == 1) {
            circuit.append(Opcode::U,
                           {random_below(rng, n)},
                           {random_angle(rng), random_angle(rng), random_angle(rng)});
        } else if (choice == 2) {
            circuit.append(
                rotation_1q[random_below(rng, 4)], {random_below(rng, n)}, {random_angle(rng)});
        } else {
            circuit.append(clifford_1q[random_below(rng, 8)], {random_below(rng, n)});
        }
    }
    return circuit;
}

// n (a qubit count) and num_terms (an observable term count) are not
// realistically confusable at any call site.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
Observable make_random_observable(std::mt19937_64& rng, std::uint32_t n, std::size_t num_terms) {
    Observable obs;
    for (std::size_t t = 0; t < num_terms; ++t) {
        PauliString p;
        p.num_qubits = n;
        p.x_words.assign((n + 63) / 64, 0);
        p.z_words.assign((n + 63) / 64, 0);
        for (QubitId q = 0; q < n; ++q) {
            const std::uint32_t kind = random_below(rng, 4);  // I,X,Y,Z uniform
            const bool x_bit = (kind == 1 || kind == 2);
            const bool z_bit = (kind == 2 || kind == 3);
            if (x_bit) {
                p.x_words[q / 64] |= (std::uint64_t{1} << (q % 64));
            }
            if (z_bit) {
                p.z_words[q / 64] |= (std::uint64_t{1} << (q % 64));
            }
        }
        std::uniform_real_distribution<double> coeff_dist(-2.0, 2.0);
        obs.terms.emplace_back(p, std::complex<double>{coeff_dist(rng), 0.0});
    }
    return obs;
}

TEST(PauliPropagationEngineDifferentialTest, MatchesDenseOracleAcrossRandomCircuits) {
    constexpr std::uint64_t kSeed = 20260922;
    std::mt19937_64 seed_source(kSeed);
    const PauliPropagationEngine pauli_engine;
    const DenseEngine dense_engine;

    for (std::uint32_t n = 1; n <= 4; ++n) {
        for (int trial = 0; trial < 5; ++trial) {
            const std::uint64_t circuit_seed = seed_source();
            std::mt19937_64 observable_rng(seed_source());
            const Circuit circuit = make_random_propagation_circuit(circuit_seed, n);
            const Observable obs = make_random_observable(observable_rng, n, 2);

            const EngineEstimateRequest est_request{
                .num_qubits = n,
                .operations = &circuit.operations(),
                .want_statevector = false,
                .want_counts = false,
                .want_expectation = true,
                .observable = &obs,
            };
            const CostEstimate estimate = pauli_engine.estimate(est_request);
            ASSERT_TRUE(estimate.feasible) << "seed=" << circuit_seed << " n=" << n;

            const EngineExecutionRequest pauli_request{
                .num_qubits = n,
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
                .observable = &obs,
            };
            const std::complex<double> pauli_value =
                combined(obs, terms_of(pauli_engine.execute(pauli_request)));

            const EngineExecutionRequest dense_request{
                .num_qubits = n,
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
                .observable = &obs,
            };
            const std::complex<double> dense_value =
                combined(obs, terms_of(dense_engine.execute(dense_request)));

            EXPECT_NEAR(pauli_value.real(), dense_value.real(), 1e-9)
                << "seed=" << circuit_seed << " n=" << n;
            EXPECT_NEAR(pauli_value.imag(), dense_value.imag(), 1e-9)
                << "seed=" << circuit_seed << " n=" << n;
        }
    }
}

TEST(PauliPropagationEngineDifferentialTest, CliffordPlusTMatchesDenseOracle) {
    // T = P(pi/4) exactly (matrix_t and matrix_p agree at lambda=pi/4,
    // src/ir/gate_table.cpp), the informal "Clifford+T" acceptance case.
    constexpr double kPiOver4 = 0.7853981633974483;
    Circuit circuit(2, 0);
    circuit.append(Opcode::H, {0});
    circuit.append(Opcode::CX, {0, 1});
    circuit.append(Opcode::P, {1}, {kPiOver4});
    circuit.append(Opcode::S, {0});

    PauliString zz;
    zz.num_qubits = 2;
    zz.x_words = {0};
    zz.z_words = {0b11};
    const Observable obs{{{zz, {1.0, 0.0}}}};

    const PauliPropagationEngine pauli_engine;
    const EngineExecutionRequest pauli_request{
        .num_qubits = 2,
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
        .observable = &obs,
    };
    const std::complex<double> pauli_value =
        combined(obs, terms_of(pauli_engine.execute(pauli_request)));

    const DenseEngine dense_engine;
    const EngineExecutionRequest dense_request{
        .num_qubits = 2,
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
        .observable = &obs,
    };
    const std::complex<double> dense_value =
        combined(obs, terms_of(dense_engine.execute(dense_request)));

    EXPECT_NEAR(pauli_value.real(), dense_value.real(), 1e-12);
    EXPECT_NEAR(pauli_value.imag(), dense_value.imag(), 1e-12);
}

}  // namespace
}  // namespace menos::engines::pauli
