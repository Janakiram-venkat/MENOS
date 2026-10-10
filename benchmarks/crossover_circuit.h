#pragma once

// The CPU/CUDA crossover circuit family (docs/adr/0014 section 14): kLayers
// layers of (H on every qubit, T on q0, CX on alternating pairs). Shared by
// cuda_crossover_benchmark.cpp and cuda_kernel_benchmark.cpp so the two
// measure the same circuits; tools/dashboard/menos_dashboard.py builds the
// same family in Qiskit.

#include <cstdint>

#include "menos/circuit.h"

namespace menos::benchmarks {

constexpr int kCrossoverLayers = 6;

inline menos::Circuit make_crossover_circuit(std::uint32_t num_qubits) {
    menos::Circuit circuit(num_qubits, 0);
    for (int layer = 0; layer < kCrossoverLayers; ++layer) {
        for (std::uint32_t q = 0; q < num_qubits; ++q) {
            circuit.append(menos::Opcode::H, {static_cast<menos::QubitId>(q)});
        }
        circuit.append(menos::Opcode::T, {0});
        for (std::uint32_t q = static_cast<std::uint32_t>(layer % 2); q + 1 < num_qubits; q += 2) {
            circuit.append(menos::Opcode::CX,
                           {static_cast<menos::QubitId>(q), static_cast<menos::QubitId>(q + 1)});
        }
    }
    return circuit;
}

}  // namespace menos::benchmarks
