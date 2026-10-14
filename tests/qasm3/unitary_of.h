#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>
#include <vector>

#include "ir/gate_table.h"
#include "menos/circuit.h"

namespace menos::qasm3::testing {

using Complex = std::complex<double>;
using Matrix = std::vector<Complex>;  // row-major, bit q of an index is qubit q

inline Matrix identity(std::uint32_t num_qubits) {
    const std::size_t dim = std::size_t{1} << num_qubits;
    Matrix m(dim * dim);
    for (std::size_t i = 0; i < dim; ++i) {
        m[i * dim + i] = 1.0;
    }
    return m;
}

inline Matrix multiply(const Matrix& a, const Matrix& b, std::size_t dim) {
    Matrix out(dim * dim);
    for (std::size_t i = 0; i < dim; ++i) {
        for (std::size_t k = 0; k < dim; ++k) {
            const Complex aik = a[i * dim + k];
            for (std::size_t j = 0; j < dim; ++j) {
                out[i * dim + j] += aik * b[k * dim + j];
            }
        }
    }
    return out;
}

// The num_qubits-qubit matrix of `gate` (bit j of its index is qubits[j]).
inline Matrix embed(std::span<const Complex> gate,
                    const std::vector<QubitId>& qubits,
                    std::uint32_t num_qubits) {
    const std::size_t dim = std::size_t{1} << num_qubits;
    const std::size_t k = qubits.size();
    const auto sub_index = [&](std::size_t full) {
        std::size_t sub = 0;
        for (std::size_t j = 0; j < k; ++j) {
            sub |= ((full >> qubits[j]) & 1U) << j;
        }
        return sub;
    };
    std::size_t mask = 0;
    for (const QubitId q : qubits) {
        mask |= std::size_t{1} << q;
    }
    Matrix out(dim * dim);
    for (std::size_t row = 0; row < dim; ++row) {
        for (std::size_t col = 0; col < dim; ++col) {
            if ((row & ~mask) == (col & ~mask)) {
                out[row * dim + col] =
                    gate[sub_index(row) * (std::size_t{1} << k) + sub_index(col)];
            }
        }
    }
    return out;
}

// The unitary of circuit's operations, global phase included.
inline Matrix unitary_of(const Circuit& circuit) {
    const std::uint32_t n = circuit.num_qubits();
    const std::size_t dim = std::size_t{1} << n;
    Matrix total = identity(n);
    for (const Operation& op : circuit.operations()) {
        Matrix step;
        if (const auto* gate = std::get_if<GateOp>(&op)) {
            step = embed(ir::generate_matrix(gate->opcode, gate->params), gate->qubits, n);
        } else {
            const auto& unitary = std::get<UnitaryOp>(op);
            step = embed(unitary.matrix, unitary.qubits, n);
        }
        total = multiply(step, total, dim);
    }
    const Complex phase = std::polar(1.0, circuit.global_phase);
    for (Complex& entry : total) {
        entry *= phase;
    }
    return total;
}

// |c..c> <c..c| (x) gate + (I - |c..c><c..c|) (x) I on num_qubits qubits:
// `gate` acts on targets when every control qubit equals its required value.
inline Matrix controlled_reference(std::span<const Complex> gate,
                                   const std::vector<QubitId>& controls,
                                   const std::vector<bool>& on_one,
                                   const std::vector<QubitId>& targets,
                                   std::uint32_t num_qubits) {
    const std::size_t dim = std::size_t{1} << num_qubits;
    const Matrix applied = embed(gate, targets, num_qubits);
    Matrix out(dim * dim);
    for (std::size_t col = 0; col < dim; ++col) {
        bool active = true;
        for (std::size_t i = 0; i < controls.size(); ++i) {
            active = active && (((col >> controls[i]) & 1U) == (on_one[i] ? 1U : 0U));
        }
        for (std::size_t row = 0; row < dim; ++row) {
            out[row * dim + col] = active ? applied[row * dim + col] : (row == col ? 1.0 : 0.0);
        }
    }
    return out;
}

inline double max_abs_difference(const Matrix& a, const Matrix& b) {
    double worst = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        worst = std::max(worst, std::abs(a[i] - b[i]));
    }
    return worst;
}

}  // namespace menos::qasm3::testing
