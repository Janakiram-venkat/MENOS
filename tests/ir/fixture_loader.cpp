#include "fixture_loader.h"

#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <unordered_map>

namespace menos::testutil {
namespace {

using Json = nlohmann::json;

const std::unordered_map<std::string, Opcode>& opcode_by_name() {
    static const std::unordered_map<std::string, Opcode> kTable{
        {"x", Opcode::X},   {"y", Opcode::Y},       {"z", Opcode::Z},   {"h", Opcode::H},
        {"s", Opcode::S},   {"sdg", Opcode::Sdg},   {"t", Opcode::T},   {"tdg", Opcode::Tdg},
        {"sx", Opcode::SX}, {"sxdg", Opcode::SXdg}, {"rx", Opcode::RX}, {"ry", Opcode::RY},
        {"rz", Opcode::RZ}, {"p", Opcode::P},       {"u", Opcode::U},   {"cx", Opcode::CX},
        {"cz", Opcode::CZ}, {"swap", Opcode::SWAP}, {"cp", Opcode::CP}, {"ccx", Opcode::CCX},
    };
    return kTable;
}

std::vector<QubitId> parse_qubits(const Json& op) {
    std::vector<QubitId> qubits;
    for (const auto& q : op.at("qubits")) {
        qubits.push_back(q.get<QubitId>());
    }
    return qubits;
}

void append_unitary_op(Circuit& circuit, const Json& op) {
    std::vector<QubitId> qubits = parse_qubits(op);
    const Json& matrix_real = op.at("matrix_real");
    const Json& matrix_imag = op.at("matrix_imag");
    std::vector<std::complex<double>> matrix;
    for (std::size_t r = 0; r < matrix_real.size(); ++r) {
        for (std::size_t c = 0; c < matrix_real.at(r).size(); ++c) {
            matrix.emplace_back(matrix_real.at(r).at(c).get<double>(),
                                matrix_imag.at(r).at(c).get<double>());
        }
    }
    circuit.append_unitary(std::move(qubits), std::move(matrix));
}

void append_named_gate_op(Circuit& circuit, const Json& op) {
    const std::string opcode_name = op.at("opcode").get<std::string>();
    const auto it = opcode_by_name().find(opcode_name);
    if (it == opcode_by_name().end()) {
        throw std::runtime_error("load_fixture_file: unknown opcode \"" + opcode_name + "\"");
    }
    std::vector<QubitId> qubits = parse_qubits(op);
    std::vector<double> params;
    for (const auto& p : op.at("params")) {
        params.push_back(p.get<double>());
    }
    circuit.append(it->second, std::move(qubits), std::move(params));
}

// Builds the observable's Pauli terms directly from the fixture's own
// per-qubit x_qubits/z_qubits index lists -- tools/gen_fixtures.py
// emits these via Pauli((z_bits, x_bits)) with no Pauli label string
// anywhere (docs/adr/0008), so this loader never parses one either.
std::optional<Observable> parse_observable(const Json& c, std::uint32_t num_qubits) {
    const auto it = c.find("observable");
    if (it == c.end()) {
        return std::nullopt;
    }
    const std::size_t words = (static_cast<std::size_t>(num_qubits) + 63) / 64;
    Observable observable;
    for (const auto& term : it->at("terms")) {
        PauliString pauli{
            num_qubits, std::vector<std::uint64_t>(words, 0), std::vector<std::uint64_t>(words, 0)};
        for (const auto& q : term.at("x_qubits")) {
            const auto qubit = q.get<std::uint32_t>();
            pauli.x_words[qubit / 64] |= (std::uint64_t{1} << (qubit % 64));
        }
        for (const auto& q : term.at("z_qubits")) {
            const auto qubit = q.get<std::uint32_t>();
            pauli.z_words[qubit / 64] |= (std::uint64_t{1} << (qubit % 64));
        }
        const std::complex<double> coeff{term.at("coeff_real").get<double>(),
                                         term.at("coeff_imag").get<double>()};
        observable.terms.emplace_back(std::move(pauli), coeff);
    }
    return observable;
}

FixtureCircuit parse_fixture_circuit(const Json& c) {
    const auto num_qubits = c.at("num_qubits").get<std::uint32_t>();
    Circuit circuit(num_qubits, 0);
    circuit.global_phase = c.at("global_phase").get<double>();

    for (const auto& op : c.at("operations")) {
        if (op.at("opcode").get<std::string>() == "unitary") {
            append_unitary_op(circuit, op);
        } else {
            append_named_gate_op(circuit, op);
        }
    }

    FixtureCircuit fixture{std::move(circuit), {}, std::nullopt, std::nullopt, std::nullopt};
    const Json& sv_real = c.at("expected_statevector_real");
    const Json& sv_imag = c.at("expected_statevector_imag");
    for (std::size_t i = 0; i < sv_real.size(); ++i) {
        fixture.expected_statevector.emplace_back(sv_real.at(i).get<double>(),
                                                  sv_imag.at(i).get<double>());
    }

    fixture.observable = parse_observable(c, num_qubits);
    if (const auto it = c.find("expected_expectation_real"); it != c.end()) {
        fixture.expected_expectation = std::complex<double>{
            it->get<double>(), c.at("expected_expectation_imag").get<double>()};
    }
    if (const auto it = c.find("qasm2_text"); it != c.end()) {
        fixture.qasm2_text = it->get<std::string>();
    }
    return fixture;
}

}  // namespace

FixtureFile load_fixture_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("load_fixture_file: cannot open " + path);
    }
    Json root;
    in >> root;

    FixtureFile file;
    file.qiskit_version = root.at("qiskit_version").get<std::string>();
    for (const auto& c : root.at("circuits")) {
        file.circuits.push_back(parse_fixture_circuit(c));
    }
    return file;
}

}  // namespace menos::testutil
