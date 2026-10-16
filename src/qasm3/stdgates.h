#pragma once

#include <span>
#include <string_view>
#include <vector>

#include "qasm3/modifiers.h"

namespace menos::qasm3::detail {

struct StdGate {
    std::string_view name;
    int num_params;
    int num_qubits;
};

// The `stdgates.inc` gate called name, or nullptr if there is none.
[[nodiscard]] const StdGate* find_stdgate(std::string_view name);

// The operations of `stdgates.inc` gate `gate` on `qubits`. Each has the
// matrix of Qiskit's gate of the same name, global phase included
// (docs/adr/0023 section 3): a gate-table gate where one exists (phase and u1
// are p, cphase is cp, CX is cx, u2 and u3 are u, id is nothing), an exact
// decomposition into gate-table gates (cy, ch, crx, cry, crz, cswap), or an
// exact UnitaryOp (cu). Preconditions: params.size() == gate.num_params and
// qubits.size() == gate.num_qubits.
[[nodiscard]] std::vector<Instr> expand_stdgate(const StdGate& gate,
                                                std::span<const double> params,
                                                std::span<const QubitId> qubits);

}  // namespace menos::qasm3::detail
