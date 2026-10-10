#include "ir/validate.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <set>
#include <string>
#include <unordered_set>

#include "common/error.h"
#include "common/tolerance.h"
#include "ir/gate_table.h"

namespace menos::ir {
namespace {

using common::InvalidCircuit;
using common::UnsupportedFeature;

std::string at(std::size_t index) {
    return "operation " + std::to_string(index) + ": ";
}

void check_qubits_in_range(const std::vector<QubitId>& qubits,
                           std::uint32_t num_qubits,
                           std::size_t op_index) {
    for (QubitId q : qubits) {
        if (q >= num_qubits) {
            throw InvalidCircuit(at(op_index) + "qubit index " + std::to_string(q) +
                                 " is out of range for a " + std::to_string(num_qubits) +
                                 "-qubit circuit");
        }
    }
}

void check_no_repeated_operand(const std::vector<QubitId>& qubits, std::size_t op_index) {
    std::unordered_set<QubitId> seen;
    for (QubitId q : qubits) {
        if (!seen.insert(q).second) {
            throw InvalidCircuit(at(op_index) + "qubit " + std::to_string(q) +
                                 " is repeated within one operation");
        }
    }
}

void check_finite_params(const std::vector<double>& params, std::size_t op_index) {
    for (double p : params) {
        if (!std::isfinite(p)) {
            throw InvalidCircuit(at(op_index) + "parameter is not finite");
        }
    }
}

bool is_unitary(const std::vector<std::complex<double>>& m, std::size_t dim) {
    for (std::size_t r = 0; r < dim; ++r) {
        for (std::size_t c = 0; c < dim; ++c) {
            std::complex<double> sum{0.0, 0.0};
            for (std::size_t k = 0; k < dim; ++k) {
                sum += m[r * dim + k] * std::conj(m[c * dim + k]);
            }
            const std::complex<double> expected =
                (r == c) ? std::complex<double>{1.0, 0.0} : std::complex<double>{0.0, 0.0};
            if (std::abs(sum - expected) > common::kDefaultAbsTol) {
                return false;
            }
        }
    }
    return true;
}

void check_gate_op(const GateOp& op, const Circuit& circuit, std::size_t op_index) {
    if (op.opcode == Opcode::Unitary || op.opcode == Opcode::Measure) {
        throw InvalidCircuit(at(op_index) +
                             "Unitary and Measure must be appended via append_unitary()/"
                             "append_measure(), not append()");
    }
    check_qubits_in_range(op.qubits, circuit.num_qubits(), op_index);
    check_no_repeated_operand(op.qubits, op_index);

    const GateInfo& info = gate_info(op.opcode);
    if (op.qubits.size() != static_cast<std::size_t>(info.arity)) {
        throw InvalidCircuit(at(op_index) + "gate " + std::string(info.name) + " expects " +
                             std::to_string(info.arity) + " qubit(s), got " +
                             std::to_string(op.qubits.size()));
    }
    if (op.params.size() != static_cast<std::size_t>(info.num_params)) {
        throw InvalidCircuit(at(op_index) + "gate " + std::string(info.name) + " expects " +
                             std::to_string(info.num_params) + " parameter(s), got " +
                             std::to_string(op.params.size()));
    }
    check_finite_params(op.params, op_index);
}

void check_unitary_op(const UnitaryOp& op, const Circuit& circuit, std::size_t op_index) {
    check_qubits_in_range(op.qubits, circuit.num_qubits(), op_index);
    check_no_repeated_operand(op.qubits, op_index);

    const std::size_t k = op.qubits.size();
    if (k == 0) {
        throw InvalidCircuit(at(op_index) + "Unitary must act on at least one qubit");
    }
    if (k > static_cast<std::size_t>(kMaxUnitaryQubits)) {
        throw UnsupportedFeature(
            at(op_index) + "Unitary acts on " + std::to_string(k) +
            " qubits, exceeding kMaxUnitaryQubits=" + std::to_string(kMaxUnitaryQubits));
    }

    const std::size_t dim = std::size_t{1} << k;
    if (op.matrix.size() != dim * dim) {
        throw InvalidCircuit(at(op_index) + "Unitary matrix has " +
                             std::to_string(op.matrix.size()) + " entries, expected " +
                             std::to_string(dim * dim) + " for " + std::to_string(k) + " qubit(s)");
    }
    for (const std::complex<double>& entry : op.matrix) {
        if (!std::isfinite(entry.real()) || !std::isfinite(entry.imag())) {
            throw InvalidCircuit(at(op_index) + "Unitary matrix has a non-finite entry");
        }
    }
    if (!is_unitary(op.matrix, dim)) {
        throw InvalidCircuit(at(op_index) + "Unitary matrix is not unitary within tolerance");
    }
}

// Trace preservation, sum_i K_i^dagger K_i == I, checked entrywise within
// common::kDefaultAbsTol -- the same tolerance and the same entrywise form as
// is_unitary above (docs/adr/0016 section 9, decision 2).
bool is_trace_preserving(const std::vector<std::vector<std::complex<double>>>& kraus,
                         std::size_t dim) {
    for (std::size_t r = 0; r < dim; ++r) {
        for (std::size_t c = 0; c < dim; ++c) {
            std::complex<double> sum{0.0, 0.0};
            for (const std::vector<std::complex<double>>& k : kraus) {
                for (std::size_t i = 0; i < dim; ++i) {
                    sum += std::conj(k[i * dim + r]) * k[i * dim + c];
                }
            }
            const std::complex<double> expected =
                (r == c) ? std::complex<double>{1.0, 0.0} : std::complex<double>{0.0, 0.0};
            if (std::abs(sum - expected) > common::kDefaultAbsTol) {
                return false;
            }
        }
    }
    return true;
}

void check_channel_op(const ChannelOp& op, const Circuit& circuit, std::size_t op_index) {
    check_qubits_in_range(op.qubits, circuit.num_qubits(), op_index);
    check_no_repeated_operand(op.qubits, op_index);

    const std::size_t k = op.qubits.size();
    if (k == 0) {
        throw InvalidCircuit(at(op_index) + "Channel must act on at least one qubit");
    }
    if (k > static_cast<std::size_t>(kMaxUnitaryQubits)) {
        throw UnsupportedFeature(
            at(op_index) + "Channel acts on " + std::to_string(k) +
            " qubits, exceeding kMaxUnitaryQubits=" + std::to_string(kMaxUnitaryQubits));
    }
    if (op.kraus.empty()) {
        throw InvalidCircuit(at(op_index) + "Channel has no Kraus operators");
    }

    const std::size_t dim = std::size_t{1} << k;
    for (std::size_t i = 0; i < op.kraus.size(); ++i) {
        const std::vector<std::complex<double>>& matrix = op.kraus[i];
        if (matrix.size() != dim * dim) {
            throw InvalidCircuit(at(op_index) + "Channel Kraus operator " + std::to_string(i) +
                                 " has " + std::to_string(matrix.size()) + " entries, expected " +
                                 std::to_string(dim * dim) + " for " + std::to_string(k) +
                                 " qubit(s)");
        }
        for (const std::complex<double>& entry : matrix) {
            if (!std::isfinite(entry.real()) || !std::isfinite(entry.imag())) {
                throw InvalidCircuit(at(op_index) + "Channel Kraus operator " + std::to_string(i) +
                                     " has a non-finite entry");
            }
        }
    }
    if (!is_trace_preserving(op.kraus, dim)) {
        throw InvalidCircuit(at(op_index) +
                             "Channel is not trace preserving within tolerance "
                             "(sum of K^dagger K differs from the identity)");
    }
}

void check_readout_error(const ReadoutError& error, std::size_t op_index) {
    for (const double p : error.assignment) {
        if (!std::isfinite(p) || p < 0.0 || p > 1.0) {
            throw InvalidCircuit(at(op_index) +
                                 "readout error has a probability outside [0, 1] or not finite");
        }
    }
    for (std::size_t row = 0; row < 2; ++row) {
        const double sum = error.assignment[2 * row] + error.assignment[2 * row + 1];
        if (std::abs(sum - 1.0) > common::kDefaultAbsTol) {
            throw InvalidCircuit(at(op_index) + "readout error row " + std::to_string(row) +
                                 " does not sum to 1 within tolerance");
        }
    }
}

void check_measure_op(const MeasureOp& op, const Circuit& circuit, std::size_t op_index) {
    if (op.qubit >= circuit.num_qubits()) {
        throw InvalidCircuit(at(op_index) + "qubit index " + std::to_string(op.qubit) +
                             " is out of range for a " + std::to_string(circuit.num_qubits()) +
                             "-qubit circuit");
    }
    if (op.clbit >= circuit.num_clbits()) {
        throw InvalidCircuit(at(op_index) + "classical bit index " + std::to_string(op.clbit) +
                             " is out of range for a " + std::to_string(circuit.num_clbits()) +
                             "-clbit circuit");
    }
    if (op.readout_error.has_value()) {
        check_readout_error(*op.readout_error, op_index);
    }
}

}  // namespace

void validate(const Circuit& circuit) {
    if (circuit.num_qubits() < 1) {
        throw InvalidCircuit("circuit has zero qubits; at least one is required");
    }
    std::unordered_set<QubitId> measured_qubits;
    const std::vector<Operation>& ops = circuit.operations();
    for (std::size_t i = 0; i < ops.size(); ++i) {
        std::visit(
            [&](const auto& op) {
                using T = std::decay_t<decltype(op)>;
                if constexpr (std::is_same_v<T, GateOp>) {
                    check_gate_op(op, circuit, i);
                    for (QubitId q : op.qubits) {
                        if (measured_qubits.contains(q)) {
                            throw UnsupportedFeature(
                                at(i) + "qubit " + std::to_string(q) +
                                " has a non-terminal measurement (an operation follows a "
                                "measurement of this qubit)");
                        }
                    }
                } else if constexpr (std::is_same_v<T, UnitaryOp>) {
                    check_unitary_op(op, circuit, i);
                    for (QubitId q : op.qubits) {
                        if (measured_qubits.contains(q)) {
                            throw UnsupportedFeature(
                                at(i) + "qubit " + std::to_string(q) +
                                " has a non-terminal measurement (an operation follows a "
                                "measurement of this qubit)");
                        }
                    }
                } else if constexpr (std::is_same_v<T, ChannelOp>) {
                    check_channel_op(op, circuit, i);
                    for (QubitId q : op.qubits) {
                        if (measured_qubits.contains(q)) {
                            throw UnsupportedFeature(
                                at(i) + "qubit " + std::to_string(q) +
                                " has a non-terminal measurement (an operation follows a "
                                "measurement of this qubit)");
                        }
                    }
                } else {
                    static_assert(std::is_same_v<T, MeasureOp>);
                    check_measure_op(op, circuit, i);
                    measured_qubits.insert(op.qubit);
                }
            },
            ops[i]);
    }
}

void validate_output_request(const Circuit& circuit,
                             std::uint64_t shots,
                             bool want_statevector,
                             bool want_expectation) {
    if (shots > 0) {
        const bool has_measure = std::any_of(
            circuit.operations().begin(), circuit.operations().end(), [](const Operation& op) {
                return std::holds_alternative<MeasureOp>(op);
            });
        if (!has_measure) {
            throw InvalidCircuit(
                "shots > 0 was requested but the circuit has no Measure operation");
        }
    }
    if (shots == 0 && !want_statevector && !want_expectation) {
        throw InvalidCircuit(
            "request asks for neither counts (shots=0) nor a statevector nor an expectation; "
            "nothing to produce");
    }
}

}  // namespace menos::ir
