#include "menos/circuit.h"

#include "common/error.h"

namespace menos {

// num_qubits then num_clbits is the universal convention across quantum
// SDKs (Qiskit, Cirq); every test exercises both num_qubits() and
// num_clbits().
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
Circuit::Circuit(std::uint32_t num_qubits, std::uint32_t num_clbits)
    : num_qubits_(num_qubits), num_clbits_(num_clbits) {}

void Circuit::append(Opcode opcode, std::vector<QubitId> qubits, std::vector<double> params) {
    operations_.push_back(GateOp{opcode, std::move(qubits), std::move(params)});
}

void Circuit::append_unitary(std::vector<QubitId> qubits,
                             std::vector<std::complex<double>> matrix) {
    operations_.push_back(UnitaryOp{std::move(qubits), std::move(matrix)});
}

void Circuit::append_measure(QubitId qubit,
                             ClbitId clbit,
                             std::optional<ReadoutError> readout_error) {
    operations_.push_back(MeasureOp{qubit, clbit, readout_error});
}

void Circuit::append_channel(std::vector<QubitId> qubits,
                             std::vector<std::vector<std::complex<double>>> kraus) {
    operations_.push_back(ChannelOp{std::move(qubits), std::move(kraus)});
}

void Circuit::append_batch(std::vector<Opcode> opcodes,
                           std::vector<QubitId> qubits,
                           std::vector<std::uint32_t> qubit_offsets,
                           std::vector<double> params,
                           std::vector<std::uint32_t> param_offsets) {
    const std::size_t n = opcodes.size();
    common::ensure(qubit_offsets.size() == n + 1,
                   "append_batch: qubit_offsets.size() must be opcodes.size() + 1");
    common::ensure(param_offsets.size() == n + 1,
                   "append_batch: param_offsets.size() must be opcodes.size() + 1");
    common::ensure(qubit_offsets.front() == 0, "append_batch: qubit_offsets must start at 0");
    common::ensure(param_offsets.front() == 0, "append_batch: param_offsets must start at 0");
    common::ensure(qubit_offsets.back() == qubits.size(),
                   "append_batch: qubit_offsets.back() must equal qubits.size()");
    common::ensure(param_offsets.back() == params.size(),
                   "append_batch: param_offsets.back() must equal params.size()");
    for (std::size_t i = 0; i < n; ++i) {
        common::ensure(qubit_offsets[i] <= qubit_offsets[i + 1],
                       "append_batch: qubit_offsets must be non-decreasing");
        common::ensure(param_offsets[i] <= param_offsets[i + 1],
                       "append_batch: param_offsets must be non-decreasing");
    }

    operations_.reserve(operations_.size() + n);
    for (std::size_t i = 0; i < n; ++i) {
        std::vector<QubitId> op_qubits(
            qubits.begin() + static_cast<std::ptrdiff_t>(qubit_offsets[i]),
            qubits.begin() + static_cast<std::ptrdiff_t>(qubit_offsets[i + 1]));
        std::vector<double> op_params(
            params.begin() + static_cast<std::ptrdiff_t>(param_offsets[i]),
            params.begin() + static_cast<std::ptrdiff_t>(param_offsets[i + 1]));
        operations_.push_back(GateOp{opcodes[i], std::move(op_qubits), std::move(op_params)});
    }
}

std::uint32_t Circuit::num_qubits() const {
    return num_qubits_;
}

std::uint32_t Circuit::num_clbits() const {
    return num_clbits_;
}

const std::vector<Operation>& Circuit::operations() const {
    return operations_;
}

}  // namespace menos
