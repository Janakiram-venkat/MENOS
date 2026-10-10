#include "engines/mps/expectation.h"

#include <gtest/gtest.h>

#include <complex>
#include <type_traits>
#include <variant>
#include <vector>

#include "common/pauli_row.h"
#include "engines/dense/apply_matrix.h"
#include "engines/dense/state_vector.h"
#include "engines/mps/apply_gate.h"
#include "ir/gate_table.h"
#include "menos/circuit.h"
#include "menos/observable.h"

namespace menos::engines::mps {
namespace {

using Complex = std::complex<double>;
constexpr double kTol = 1e-9;

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

MPSState run_on_mps(const Circuit& circuit) {
    MPSState state(circuit.num_qubits());
    const AccuracyPolicy exact;
    TruncationStats stats;
    for (const Operation& op : circuit.operations()) {
        std::visit(
            [&](const auto& o) {
                using T = std::decay_t<decltype(o)>;
                if constexpr (std::is_same_v<T, GateOp>) {
                    const std::vector<Complex> matrix = ir::generate_matrix(o.opcode, o.params);
                    apply_gate(state, matrix, o.qubits, exact, stats);
                }
            },
            op);
    }
    return state;
}

// Independent oracle: apply each of the Pauli string's own per-qubit
// 1-qubit matrices to a copy of the dense ket, then take the inner
// product against the original bra -- <psi|P|psi>, using only
// primitives already validated elsewhere (dense::apply_matrix,
// ir::generate_matrix), never the MPS code under test.
std::complex<double> dense_expectation(const std::vector<Complex>& psi,
                                       const PauliString& pauli,
                                       std::uint32_t num_qubits) {
    std::vector<Complex> p_psi = psi;
    for (std::uint32_t q = 0; q < num_qubits; ++q) {
        const bool x = common::get_bit(pauli.x_words, q);
        const bool z = common::get_bit(pauli.z_words, q);
        std::vector<Complex> matrix;
        if (!x && !z) {
            continue;  // Identity: no-op.
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

PauliString make_pauli(std::uint32_t num_qubits, const std::vector<std::pair<QubitId, char>>& ops) {
    PauliString pauli;
    pauli.num_qubits = num_qubits;
    pauli.x_words.assign((num_qubits + 63) / 64, 0);
    pauli.z_words.assign((num_qubits + 63) / 64, 0);
    for (const auto& [q, c] : ops) {
        switch (c) {
            case 'X':
                common::set_bit(pauli.x_words, q, true);
                break;
            case 'Y':
                common::set_bit(pauli.x_words, q, true);
                common::set_bit(pauli.z_words, q, true);
                break;
            case 'Z':
                common::set_bit(pauli.z_words, q, true);
                break;
            default:
                break;
        }
    }
    return pauli;
}

TEST(ExpectationTest, IdentityOnNormalizedStateIsOne) {
    Circuit circuit(3, 0);
    circuit.append(Opcode::H, {0});
    circuit.append(Opcode::CX, {0, 1});
    circuit.append(Opcode::RY, {2}, {0.5});
    const MPSState state = run_on_mps(circuit);
    const PauliString identity = make_pauli(3, {});
    const Complex value = compute_expectation(state, identity);
    EXPECT_NEAR(value.real(), 1.0, kTol);
    EXPECT_NEAR(value.imag(), 0.0, kTol);
}

TEST(ExpectationTest, ZOnPlusStateIsZero) {
    Circuit circuit(1, 0);
    circuit.append(Opcode::H, {0});
    const MPSState state = run_on_mps(circuit);
    const PauliString z = make_pauli(1, {{0, 'Z'}});
    const Complex value = compute_expectation(state, z);
    EXPECT_NEAR(value.real(), 0.0, kTol);
    EXPECT_NEAR(value.imag(), 0.0, kTol);
}

TEST(ExpectationTest, XOnPlusStateIsOne) {
    Circuit circuit(1, 0);
    circuit.append(Opcode::H, {0});
    const MPSState state = run_on_mps(circuit);
    const PauliString x = make_pauli(1, {{0, 'X'}});
    const Complex value = compute_expectation(state, x);
    EXPECT_NEAR(value.real(), 1.0, kTol);
    EXPECT_NEAR(value.imag(), 0.0, kTol);
}

TEST(ExpectationTest, MixedMultiQubitObservableMatchesDenseOracle) {
    Circuit circuit(4, 0);
    circuit.append(Opcode::H, {0});
    circuit.append(Opcode::CX, {0, 1});
    circuit.append(Opcode::RY, {2}, {1.1});
    circuit.append(Opcode::CX, {2, 3});
    circuit.append(Opcode::CX, {1, 2});  // entangle across the whole chain
    circuit.append(Opcode::T, {3});

    const std::vector<Complex> dense_state = run_on_dense(circuit);
    const MPSState mps_state = run_on_mps(circuit);

    const PauliString observable = make_pauli(4, {{0, 'X'}, {1, 'Z'}, {2, 'Y'}, {3, 'Z'}});
    const Complex expected = dense_expectation(dense_state, observable, 4);
    const Complex actual = compute_expectation(mps_state, observable);
    EXPECT_NEAR(actual.real(), expected.real(), kTol);
    EXPECT_NEAR(actual.imag(), expected.imag(), kTol);
}

TEST(ExpectationTest, NonAdjacentAndPartialSupportObservableMatchesDenseOracle) {
    Circuit circuit(5, 0);
    circuit.append(Opcode::H, {0});
    circuit.append(Opcode::CX, {0, 2});  // non-adjacent, exercises MPS's SWAP network
    circuit.append(Opcode::RY, {1}, {0.4});
    circuit.append(Opcode::CX, {3, 4});
    circuit.append(Opcode::CCX, {0, 4, 2});

    const std::vector<Complex> dense_state = run_on_dense(circuit);
    const MPSState mps_state = run_on_mps(circuit);

    // Only 3 of the 5 qubits appear in the term (qubits 1, 3 carry
    // implicit identity) -- exercises a genuinely partial-support Pauli
    // string, not just a full-weight one.
    const PauliString observable = make_pauli(5, {{0, 'Z'}, {2, 'X'}, {4, 'Y'}});
    const Complex expected = dense_expectation(dense_state, observable, 5);
    const Complex actual = compute_expectation(mps_state, observable);
    EXPECT_NEAR(actual.real(), expected.real(), kTol);
    EXPECT_NEAR(actual.imag(), expected.imag(), kTol);
}

}  // namespace
}  // namespace menos::engines::mps
