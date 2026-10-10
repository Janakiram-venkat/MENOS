// Every pure-state engine refuses a noisy region with a reason, rather than
// running it with its noise dropped (Phase 12, docs/adr/0016 section 7 risk 1).
// A noisy region is one holding a ChannelOp or a MeasureOp with a readout
// error. The request asks for an expectation value and no counts, the one
// request every engine (including Pauli propagation and the tensor network)
// otherwise accepts, so the refusal can only come from the noise check.

#include <gtest/gtest.h>

#include <complex>
#include <cstdint>
#include <string>
#include <vector>

#include "menos/observable.h"
#include "menos/simulation_engine.h"
#include "plan/available_engines.h"

namespace menos::plan {
namespace {

// Z on qubit 0 of a circuit with at most 64 qubits (one bit-packed word).
Observable z_on_qubit_0(std::uint32_t num_qubits) {
    Observable observable;
    observable.terms.emplace_back(PauliString{num_qubits, {0}, {1}},
                                  std::complex<double>{1.0, 0.0});
    return observable;
}

EngineEstimateRequest expectation_request(const std::vector<Operation>& operations,
                                          const Observable& observable) {
    return EngineEstimateRequest{.num_qubits = 2,
                                 .operations = &operations,
                                 .want_statevector = false,
                                 .want_counts = false,
                                 .want_expectation = true,
                                 .observable = &observable};
}

TEST(EngineNoiseRefusalTest, EveryEngineRefusesANoiseChannel) {
    Circuit circuit(2, 0);
    circuit.append(Opcode::RZ, {0}, {0.3});  // accepted by every engine, Pauli included
    circuit.append(Opcode::CX, {0, 1});
    circuit.append_channel({1}, {{1.0, 0.0, 0.0, 1.0}});
    const Observable observable = z_on_qubit_0(2);
    const EngineEstimateRequest request = expectation_request(circuit.operations(), observable);

    for (const SimulationEngine* engine : available_engines()) {
        const CostEstimate estimate = engine->estimate(request);
        EXPECT_FALSE(estimate.feasible) << engine->name();
        EXPECT_EQ(estimate.infeasible_reason,
                  "operation 2 is a noise channel; this engine simulates pure states only")
            << engine->name();
    }
}

TEST(EngineNoiseRefusalTest, EveryEngineRefusesAReadoutError) {
    Circuit circuit(2, 1);
    circuit.append(Opcode::RZ, {0}, {0.3});
    circuit.append(Opcode::CX, {0, 1});
    circuit.append_measure(1, 0, ReadoutError{{0.9, 0.1, 0.2, 0.8}});
    const Observable observable = z_on_qubit_0(2);
    const EngineEstimateRequest request = expectation_request(circuit.operations(), observable);

    for (const SimulationEngine* engine : available_engines()) {
        const CostEstimate estimate = engine->estimate(request);
        EXPECT_FALSE(estimate.feasible) << engine->name();
        EXPECT_EQ(estimate.infeasible_reason,
                  "operation 2 is a measurement with a readout error; this engine does not "
                  "model readout error")
            << engine->name();
    }
}

// The same request without the noise is feasible for every engine except the
// stabilizer (RZ is not Clifford), so the refusals above are the noise check's.
TEST(EngineNoiseRefusalTest, TheSameRequestWithoutNoiseIsAcceptedByTheNonCliffordEngines) {
    Circuit circuit(2, 0);
    circuit.append(Opcode::RZ, {0}, {0.3});
    circuit.append(Opcode::CX, {0, 1});
    const Observable observable = z_on_qubit_0(2);
    const EngineEstimateRequest request = expectation_request(circuit.operations(), observable);

    for (const SimulationEngine* engine : available_engines()) {
        const CostEstimate estimate = engine->estimate(request);
        if (engine->engine_id() == Engine::Stabilizer) {
            EXPECT_FALSE(estimate.feasible);
        } else {
            EXPECT_TRUE(estimate.feasible) << engine->name() << ": " << estimate.infeasible_reason;
        }
    }
}

}  // namespace
}  // namespace menos::plan
