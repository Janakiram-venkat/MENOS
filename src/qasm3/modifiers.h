#pragma once

#include <complex>
#include <cstdint>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

#include "menos/circuit.h"

namespace menos::qasm3::detail {

// A phase e^{i angle} on the whole state: `gphase`, or a gate's own global
// phase. It is observable only once a gate containing it is controlled.
struct GlobalPhase {
    double angle;
};

// One step of an expanded gate call.
using Instr = std::variant<GateOp, UnitaryOp, GlobalPhase>;

// The exact inverse of body: the steps in reverse order, each inverted
// through the gate table's own inverses (s <-> sdg, rx(t) -> rx(-t),
// u(t, p, l) -> u(-t, -l, -p), ...), a UnitaryOp by its conjugate transpose.
[[nodiscard]] std::vector<Instr> invert(std::vector<Instr> body);

// body applied k times; a negative k applies invert(body) |k| times.
[[nodiscard]] std::vector<Instr> power(const std::vector<Instr>& body, std::int64_t k);

// body controlled on `control` being |1>, step by step (the controlled
// version of a product is the product of the controlled steps):
// x -> cx -> ccx, z -> cz, p -> cp, a global phase -> p on the control,
// anything else a UnitaryOp on {control, its qubits}. Throws
// common::UnsupportedFeature, its message ending in `where`, if that UnitaryOp
// would exceed kMaxUnitaryQubits qubits.
[[nodiscard]] std::vector<Instr> control(const std::vector<Instr>& body,
                                         QubitId control,
                                         std::string_view where);

// body controlled on `control` being |0>: x on the control, control(), x.
[[nodiscard]] std::vector<Instr> negative_control(const std::vector<Instr>& body,
                                                  QubitId control,
                                                  std::string_view where);

// The 2^(k+1) x 2^(k+1) matrix |0><0| (x) I + |1><1| (x) m for a 2^k x 2^k
// row-major m, in the UnitaryOp bit convention with the control as bit 0.
[[nodiscard]] std::vector<std::complex<double>> controlled_matrix(
    std::span<const std::complex<double>> m);

}  // namespace menos::qasm3::detail
