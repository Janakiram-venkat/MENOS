#include "qasm3/stdgates.h"

#include <array>
#include <complex>
#include <numbers>

#include "common/error.h"
#include "ir/gate_table.h"

namespace menos::qasm3::detail {
namespace {

constexpr std::array kStdGates = {
    StdGate{"p", 1, 1},   StdGate{"x", 0, 1},    StdGate{"y", 0, 1},     StdGate{"z", 0, 1},
    StdGate{"h", 0, 1},   StdGate{"s", 0, 1},    StdGate{"sdg", 0, 1},   StdGate{"t", 0, 1},
    StdGate{"tdg", 0, 1}, StdGate{"sx", 0, 1},   StdGate{"rx", 1, 1},    StdGate{"ry", 1, 1},
    StdGate{"rz", 1, 1},  StdGate{"cx", 0, 2},   StdGate{"cy", 0, 2},    StdGate{"cz", 0, 2},
    StdGate{"cp", 1, 2},  StdGate{"crx", 1, 2},  StdGate{"cry", 1, 2},   StdGate{"crz", 1, 2},
    StdGate{"ch", 0, 2},  StdGate{"swap", 0, 2}, StdGate{"ccx", 0, 3},   StdGate{"cswap", 0, 3},
    StdGate{"cu", 4, 2},  StdGate{"CX", 0, 2},   StdGate{"phase", 1, 1}, StdGate{"cphase", 1, 2},
    StdGate{"id", 0, 1},  StdGate{"u1", 1, 1},   StdGate{"u2", 2, 1},    StdGate{"u3", 3, 1},
};

GateOp gate(Opcode opcode, std::vector<QubitId> qubits, std::vector<double> params = {}) {
    return GateOp{opcode, std::move(qubits), std::move(params)};
}

}  // namespace

const StdGate* find_stdgate(std::string_view name) {
    for (const StdGate& g : kStdGates) {
        if (g.name == name) {
            return &g;
        }
    }
    return nullptr;
}

std::vector<Instr> expand_stdgate(const StdGate& g,
                                  std::span<const double> params,
                                  std::span<const QubitId> qubits) {
    common::ensure(params.size() == static_cast<std::size_t>(g.num_params) &&
                       qubits.size() == static_cast<std::size_t>(g.num_qubits),
                   "qasm3: expand_stdgate called with the wrong operand counts");
    const std::string_view n = g.name;
    const QubitId a = qubits[0];
    const QubitId b = qubits.size() > 1 ? qubits[1] : a;
    const QubitId c = qubits.size() > 2 ? qubits[2] : a;
    const double t = params.empty() ? 0.0 : params[0];
    constexpr double kPi = std::numbers::pi;

    // Names with a gate-table entry of the same matrix.
    struct Direct {
        std::string_view name;
        Opcode opcode;
    };
    static constexpr std::array kDirect = {
        Direct{"p", Opcode::P},     Direct{"x", Opcode::X},       Direct{"y", Opcode::Y},
        Direct{"z", Opcode::Z},     Direct{"h", Opcode::H},       Direct{"s", Opcode::S},
        Direct{"sdg", Opcode::Sdg}, Direct{"t", Opcode::T},       Direct{"tdg", Opcode::Tdg},
        Direct{"sx", Opcode::SX},   Direct{"rx", Opcode::RX},     Direct{"ry", Opcode::RY},
        Direct{"rz", Opcode::RZ},   Direct{"cx", Opcode::CX},     Direct{"cz", Opcode::CZ},
        Direct{"cp", Opcode::CP},   Direct{"swap", Opcode::SWAP}, Direct{"ccx", Opcode::CCX},
        Direct{"CX", Opcode::CX},   Direct{"phase", Opcode::P},   Direct{"cphase", Opcode::CP},
        Direct{"u1", Opcode::P},    Direct{"u3", Opcode::U},
    };
    for (const Direct& d : kDirect) {
        if (d.name == n) {
            return {gate(d.opcode, {qubits.begin(), qubits.end()}, {params.begin(), params.end()})};
        }
    }

    if (n == "id") {
        return {};
    }
    if (n == "u2") {
        return {gate(Opcode::U, {a}, {kPi / 2, params[0], params[1]})};
    }
    if (n == "cy") {
        // S X Sdg = Y exactly.
        return {gate(Opcode::Sdg, {b}), gate(Opcode::CX, {a, b}), gate(Opcode::S, {b})};
    }
    if (n == "ch") {
        // RY(pi/4) Z RY(-pi/4) = (X + Z)/sqrt(2) = H exactly.
        return {gate(Opcode::RY, {b}, {-kPi / 4}),
                gate(Opcode::CZ, {a, b}),
                gate(Opcode::RY, {b}, {kPi / 4})};
    }
    if (n == "crz") {
        // X RZ(-t/2) X RZ(t/2) = RZ(t); RZ(-t/2) RZ(t/2) = I.
        return {gate(Opcode::RZ, {b}, {t / 2}),
                gate(Opcode::CX, {a, b}),
                gate(Opcode::RZ, {b}, {-t / 2}),
                gate(Opcode::CX, {a, b})};
    }
    if (n == "cry") {
        return {gate(Opcode::RY, {b}, {t / 2}),
                gate(Opcode::CX, {a, b}),
                gate(Opcode::RY, {b}, {-t / 2}),
                gate(Opcode::CX, {a, b})};
    }
    if (n == "crx") {
        // H RZ(t) H = RX(t), so crx is crz conjugated by H on the target.
        return {gate(Opcode::H, {b}),
                gate(Opcode::RZ, {b}, {t / 2}),
                gate(Opcode::CX, {a, b}),
                gate(Opcode::RZ, {b}, {-t / 2}),
                gate(Opcode::CX, {a, b}),
                gate(Opcode::H, {b})};
    }
    if (n == "cswap") {
        return {gate(Opcode::CX, {c, b}), gate(Opcode::CCX, {a, b, c}), gate(Opcode::CX, {c, b})};
    }
    if (n == "cu") {
        // Qiskit's CUGate: |0><0| (x) I + |1><1| (x) e^{i gamma} U(theta, phi, lambda),
        // the control (a) as bit 0.
        std::vector<std::complex<double>> u =
            ir::generate_matrix(Opcode::U, std::array{params[0], params[1], params[2]});
        const std::complex<double> phase = std::polar(1.0, params[3]);
        for (auto& entry : u) {
            entry *= phase;
        }
        return {UnitaryOp{{a, b}, controlled_matrix(u)}};
    }
    common::ensure(false, "qasm3: a stdgates.inc name has no expansion");
    return {};
}

}  // namespace menos::qasm3::detail
