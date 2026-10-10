// docs/BUILD_PLAN.md Phase 8 acceptance criterion 1 (closed-network
// expectation matches an independent oracle): hand-computable cases with
// exact analytic expected values (CLAUDE.md's "analytic result" oracle
// category), covering a trivial (identity) observable, a real
// single-qubit observable, a complex one (Y, to exercise the bra-side
// conjugation), and a two-qubit observable spanning an entangled Bell
// pair (to exercise a genuinely shared, non-pendant elimination step).
// Acceptance criterion 3 (determinism) is also covered here at the
// contraction-execution level, not only the elimination-heuristic level
// tests/analysis/treewidth_test.cpp already covers.

#include "engines/tensor_network/contraction.h"

#include <gtest/gtest.h>

#include <array>
#include <vector>

#include "analysis/treewidth.h"
#include "common/error.h"
#include "common/pauli_row.h"
#include "common/rng.h"
#include "engines/dense/apply_matrix.h"
#include "engines/dense/state_vector.h"
#include "ir/gate_table.h"

namespace menos::engines::tensor_network {
namespace {

using Complex = std::complex<double>;
constexpr double kTolerance = 1e-12;

Complex evaluate(const Circuit& circuit, const PauliString& term) {
    const analysis::ClosedTimelines timelines =
        analysis::build_closed_timelines(circuit.num_qubits(), circuit.operations(), term);
    const analysis::LineGraph line_graph = analysis::build_line_graph(timelines);
    const analysis::EliminationOrder order =
        analysis::estimate_treewidth_min_fill(line_graph.graph);
    ContractionNetwork network =
        build_contraction_network(circuit.num_qubits(), circuit.operations(), term);
    return contract(std::move(network), line_graph, order.order).scalar;
}

PauliString make_pauli(std::uint32_t num_qubits, std::uint64_t x_bits, std::uint64_t z_bits) {
    return PauliString{num_qubits, {x_bits}, {z_bits}};
}

// <0|H^dagger H|0> = <0|0> = 1: the trivial (fully-identity) observable
// closes the network with no Pauli-operator node at all.
TEST(ContractionTest, SingleQubitIdentityObservableGivesOne) {
    Circuit circuit(1, 0);
    circuit.append(Opcode::H, {0});

    const Complex result = evaluate(circuit, make_pauli(1, 0, 0));
    EXPECT_NEAR(result.real(), 1.0, kTolerance);
    EXPECT_NEAR(result.imag(), 0.0, kTolerance);
}

// <+|Z|+> = 0: H|0> = |+>, a real, well-known identity.
TEST(ContractionTest, SingleQubitHadamardWithZObservableGivesZero) {
    Circuit circuit(1, 0);
    circuit.append(Opcode::H, {0});

    const Complex result = evaluate(circuit, make_pauli(1, 0, 1));  // Z
    EXPECT_NEAR(result.real(), 0.0, kTolerance);
    EXPECT_NEAR(result.imag(), 0.0, kTolerance);
}

// S H |0> = (|0> + i|1>)/sqrt(2), an eigenstate of Y with eigenvalue +1
// (Y(|0>+i|1>) = i|1> + i*(-i)|0> = |0> + i|1>, hand-derived): <Y> = 1
// exactly. This is the only single-qubit Pauli with non-real entries, so
// it directly exercises this module's bra-side conjugation (not just a
// transpose) against a case where getting the conjugate wrong (versus
// getting the transpose wrong) changes the numeric result.
TEST(ContractionTest, HadamardThenSWithYObservableGivesOne) {
    Circuit circuit(1, 0);
    circuit.append(Opcode::H, {0});
    circuit.append(Opcode::S, {0});

    const Complex result = evaluate(circuit, make_pauli(1, 1, 1));  // Y: x=1, z=1
    EXPECT_NEAR(result.real(), 1.0, kTolerance);
    EXPECT_NEAR(result.imag(), 0.0, kTolerance);
}

// Bell state (|00> + |11>)/sqrt(2): <Z0> = 0 (the two branches cancel),
// <Z0 Z1> = 1 (both branches agree: +1*+1 and (-1)*(-1)). The Z0Z1 case
// specifically exercises a *shared* two-qubit observable spanning an
// entangled pair, not a per-qubit-separable one -- CLAUDE.md's own
// light-cone/weighted-sum invariant is about a different reduction step,
// but the underlying entanglement-spanning case is the same kind of
// structure this engine exists to contract without factoring by qubit.
TEST(ContractionTest, BellPairZ0ObservableGivesZero) {
    Circuit circuit(2, 0);
    circuit.append(Opcode::H, {0});
    circuit.append(Opcode::CX, {0, 1});

    const Complex result = evaluate(circuit, make_pauli(2, 0, 0b01));  // Z0
    EXPECT_NEAR(result.real(), 0.0, kTolerance);
    EXPECT_NEAR(result.imag(), 0.0, kTolerance);
}

TEST(ContractionTest, BellPairZ0Z1ObservableGivesOne) {
    Circuit circuit(2, 0);
    circuit.append(Opcode::H, {0});
    circuit.append(Opcode::CX, {0, 1});

    const Complex result = evaluate(circuit, make_pauli(2, 0, 0b11));  // Z0 Z1
    EXPECT_NEAR(result.real(), 1.0, kTolerance);
    EXPECT_NEAR(result.imag(), 0.0, kTolerance);
}

// Qubit 1 never appears in any operation and the term is identity there,
// so its own component (a |0> boundary directly wired to a <0| boundary)
// is entirely disconnected from qubit 0's own component -- this
// specifically exercises contract()'s final "multiply every surviving
// component's own scalar together" step with more than one surviving
// component (every other test above has exactly one connected network).
// X|0> = |1>, <1|Z|1> = -1, and the idle qubit's own disconnected
// component contributes a factor of 1 (<0|0> = 1), so the true answer is
// -1 * 1 = -1, not the same value a single-component network would give
// for qubit 0 alone -- a wrong disconnected-component handling (e.g.
// silently dropping one, or leaving it out of the product) would still
// pass every prior test in this file but fail this one.
TEST(ContractionTest, IdleQubitContributesAsADisconnectedComponent) {
    Circuit circuit(2, 0);
    circuit.append(Opcode::X, {0});

    const Complex result = evaluate(circuit, make_pauli(2, 0, 0b01));  // Z0
    EXPECT_NEAR(result.real(), -1.0, kTolerance);
    EXPECT_NEAR(result.imag(), 0.0, kTolerance);
}

// Acceptance criterion 3 (determinism): repeated contraction of the same
// network (rebuilt fresh each time, since contract() consumes its
// argument by value) in the same elimination order gives bit-identical
// results, and the same actual_peak_legs -- never merely "close".
TEST(ContractionTest, RepeatedContractionIsBitIdentical) {
    Circuit circuit(2, 0);
    circuit.append(Opcode::H, {0});
    circuit.append(Opcode::CX, {0, 1});
    const PauliString term = make_pauli(2, 0, 0b11);

    const analysis::ClosedTimelines timelines =
        analysis::build_closed_timelines(circuit.num_qubits(), circuit.operations(), term);
    const analysis::LineGraph line_graph = analysis::build_line_graph(timelines);
    const analysis::EliminationOrder order =
        analysis::estimate_treewidth_min_fill(line_graph.graph);

    ContractionNetwork first_network =
        build_contraction_network(circuit.num_qubits(), circuit.operations(), term);
    ContractionNetwork second_network =
        build_contraction_network(circuit.num_qubits(), circuit.operations(), term);

    const ContractionResult first = contract(std::move(first_network), line_graph, order.order);
    const ContractionResult second = contract(std::move(second_network), line_graph, order.order);

    EXPECT_EQ(first.scalar, second.scalar);
    EXPECT_EQ(first.actual_peak_legs, second.actual_peak_legs);
}

// docs/BUILD_PLAN.md Phase 8 acceptance criterion 1's "range of ...
// qubit counts/observable supports": random small circuits (2-5 qubits,
// a mix of every real-parameter and parameterized named gate, plus
// 2-qubit entanglers), each checked against an independent dense oracle
// -- apply every Pauli term's own 1-qubit matrix to a copy of the exact
// statevector, then take the inner product against the original bra --
// using only primitives already validated elsewhere (dense::apply_matrix,
// ir::generate_matrix), never this module's own code, mirroring
// tests/engines/mps/expectation_test.cpp's own dense_expectation oracle
// exactly. CLAUDE.md: "Random tests use fixed seeds and print the seed
// on failure."
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
    // No RX/RY/RZ/P/U (arbitrary real angle) here: this is an exactness
    // test against a real oracle, not a fuzz test, so angles are drawn
    // from a small fixed set covering non-Clifford cases, avoiding
    // relying on the RNG's own distribution shape for correctness.
    constexpr std::array<Opcode, 12> kSingleQubit = {Opcode::X,
                                                     Opcode::Y,
                                                     Opcode::Z,
                                                     Opcode::H,
                                                     Opcode::S,
                                                     Opcode::Sdg,
                                                     Opcode::T,
                                                     Opcode::Tdg,
                                                     Opcode::SX,
                                                     Opcode::SXdg,
                                                     Opcode::RX,
                                                     Opcode::RY};
    constexpr std::array<Opcode, 3> kTwoQubit = {Opcode::CX, Opcode::CZ, Opcode::SWAP};
    constexpr std::array<double, 4> kAngles = {0.3, 1.1, -0.7, 2.4};

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
            if (opcode == Opcode::RX || opcode == Opcode::RY) {
                const double angle =
                    kAngles.at(static_cast<std::size_t>(rng.next_uniform() * kAngles.size()));
                circuit.append(opcode, {q}, {angle});
            } else {
                circuit.append(opcode, {q});
            }
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

TEST(ContractionTest, RandomCircuitsMatchDenseOracleAcrossQubitCountsAndObservables) {
    for (std::uint32_t num_qubits = 2; num_qubits <= 5; ++num_qubits) {
        for (std::uint64_t seed = 0; seed < 6; ++seed) {
            common::Rng rng(seed * 100 + num_qubits);
            const Circuit circuit = random_circuit(num_qubits, /*num_ops=*/2 * num_qubits, rng);
            const PauliString term = random_pauli(num_qubits, rng);

            const std::vector<Complex> psi = run_on_dense(circuit);
            const Complex expected = dense_expectation(psi, term);
            const Complex actual = evaluate(circuit, term);

            EXPECT_NEAR(actual.real(), expected.real(), kTolerance)
                << "seed=" << seed << " num_qubits=" << num_qubits;
            EXPECT_NEAR(actual.imag(), expected.imag(), kTolerance)
                << "seed=" << seed << " num_qubits=" << num_qubits;
        }
    }
}

}  // namespace
}  // namespace menos::engines::tensor_network
