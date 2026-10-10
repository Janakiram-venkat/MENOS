#pragma once

#include <complex>
#include <cstdint>
#include <utility>
#include <vector>

namespace menos {

// A single Pauli string over num_qubits qubits, bit-packed into
// ceil(num_qubits/64)-word vectors (docs/BUILD_PLAN.md Phase 5). Per
// qubit j, bit j of x_words/z_words gives I/X/Z/Y as
// (0,0)/(1,0)/(0,1)/(1,1) -- matching Qiskit's Pauli convention
// Y = i*X*Z exactly (verified against a Qiskit fixture, docs/adr/0007;
// the same convention src/engines/stabilizer already uses internally
// via common::PauliRow).
struct PauliString {
    std::uint32_t num_qubits;
    std::vector<std::uint64_t> x_words;
    std::vector<std::uint64_t> z_words;
};

// A weighted sum of Pauli strings: the observable an expectation-value
// request asks for, <psi| (sum_i coefficient_i * PauliString_i) |psi>.
// Coefficients are taken as given, not validated for Hermiticity,
// matching Qiskit's SparsePauliOp (docs/BUILD_PLAN.md Phase 5).
struct Observable {
    std::vector<std::pair<PauliString, std::complex<double>>> terms;
};

}  // namespace menos
