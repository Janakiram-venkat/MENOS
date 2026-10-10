// Benchmark group (docs/BUILD_PLAN.md Phase 8, performance sanity): two
// reports about the tensor-network engine's cost model, neither of which
// belongs in the test suite (CLAUDE.md: "Benchmarks are not tests. A
// performance claim needs a reproducible benchmark in the repository with
// the hardware recorded").
//
// 1. Predicted-versus-measured. estimated_work_units against measured
//    contraction wall time, and estimated peak bytes against execute()'s
//    own measured peak, over a tree-topology family at growing depth. The
//    soundness property (estimate bounds actual) is asserted as a unit test
//    in tests/engines/tensor_network; what this adds is whether the work
//    figure tracks reality well enough to compare candidates, which is a
//    correlation question and therefore a measurement, not an assertion.
//
// 2. Planning overhead. DirectTensorNetwork is an ordinary cost-based
//    candidate, so its estimate() -- which runs a real elimination search,
//    unlike every other engine's closed-form estimate -- now runs on every
//    expectation job. An earlier attempt at this phase special-cased the
//    candidate in the planner to avoid exactly that cost; this phase keeps
//    the planner cost-driven and bounds the search instead, so the overhead
//    it leaves behind is a number that has to be reported rather than
//    assumed acceptable.

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>

#include "engines/tensor_network/tensor_network_engine.h"
#include "menos/circuit.h"
#include "menos/observable.h"
#include "menos/plan.h"
#include "menos/simulation_engine.h"
#include "menos_benchmark_environment.h"
#include "plan/hardware_profile.h"
#include "plan/job_spec.h"
#include "plan/planner.h"

namespace {

using namespace std::chrono;
using namespace menos;

// The same balanced-binary-tree family the Phase 8 acceptance tests use
// (tests/api/tensor_network_scale_test.cpp), so the numbers here describe
// the workload the phase is actually accepted against.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
Circuit make_tree_circuit(std::uint32_t n, std::uint32_t depth) {
    Circuit circuit(n, 0);
    for (std::uint32_t layer = 0; layer < depth; ++layer) {
        for (std::uint32_t q = 0; q < n; ++q) {
            const double angle =
                0.37 + 0.11 * static_cast<double>(layer) + 0.017 * static_cast<double>(q);
            circuit.append(Opcode::RY, {static_cast<QubitId>(q)}, {angle});
        }
        for (std::uint32_t parent = layer % 2; parent < n; parent += 2) {
            for (std::uint32_t k = 1; k <= 2; ++k) {
                const std::uint32_t child = 2 * parent + k;
                if (child < n) {
                    circuit.append(Opcode::CZ,
                                   {static_cast<QubitId>(parent), static_cast<QubitId>(child)});
                }
            }
        }
    }
    circuit.append(Opcode::T, {0});
    return circuit;
}

// An ordinary nearest-neighbour circuit, deliberately NOT one this engine is
// good at: the planning-overhead question is what the candidate costs jobs
// that will never choose it.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
Circuit make_ordinary_circuit(std::uint32_t n, std::uint32_t depth) {
    Circuit circuit(n, 0);
    for (std::uint32_t layer = 0; layer < depth; ++layer) {
        for (std::uint32_t q = 0; q < n; ++q) {
            circuit.append(
                Opcode::RY, {static_cast<QubitId>(q)}, {0.21 * static_cast<double>(q + 1)});
        }
        for (std::uint32_t q = layer % 2; q + 1 < n; q += 2) {
            circuit.append(Opcode::CX, {static_cast<QubitId>(q), static_cast<QubitId>(q + 1)});
        }
    }
    return circuit;
}

Observable z_on_every_qubit(std::uint32_t n) {
    PauliString p;
    p.num_qubits = n;
    p.x_words.assign((n + 63) / 64, 0);
    p.z_words.assign((n + 63) / 64, 0);
    for (std::uint32_t q = 0; q < n; ++q) {
        p.z_words[q / 64] |= (std::uint64_t{1} << (q % 64));
    }
    return Observable{{{p, {1.0, 0.0}}}};
}

EngineExecutionRequest execution_request(const Circuit& circuit, const Observable& obs) {
    return EngineExecutionRequest{
        .num_qubits = circuit.num_qubits(),
        .operations = circuit.operations(),
        .want_statevector = false,
        .want_counts = false,
        .want_expectation = true,
        .shots = 0,
        .measure_targets = {},
        .bitstring_width = 0,
        .seed = 0,
        .global_phase = 0.0,
        .budget_bytes = menos_benchmark::safe_allocation_budget_bytes(),
        .is_sole_region = true,
        .observable = &obs,
    };
}

}  // namespace

int main() {
    std::cout << menos_benchmark::describe_environment() << "\n";
    const std::uint64_t budget = menos_benchmark::safe_allocation_budget_bytes();
    std::cout << "budget_bytes=" << budget << "\n\n";

    const engines::tensor_network::TensorNetworkEngine engine;

    std::cout << "== 1. predicted versus measured, tree topology (n=15) ==\n";
    std::cout << "   depth  est_peak_bytes  actual_peak_bytes  est_work_units  measured_us\n";
    for (const std::uint32_t depth : {2u, 3u, 4u, 5u, 6u}) {
        const Circuit circuit = make_tree_circuit(15, depth);
        const Observable obs = z_on_every_qubit(15);
        const EngineEstimateRequest estimate_request{
            .num_qubits = circuit.num_qubits(),
            .operations = &circuit.operations(),
            .want_statevector = false,
            .want_counts = false,
            .want_expectation = true,
            .observable = &obs,
            .accuracy_policy = {},
            .budget_bytes = budget,
        };
        const CostEstimate estimate = engine.estimate(estimate_request);
        if (!estimate.feasible) {
            std::cout << "   " << depth << "  infeasible: " << estimate.infeasible_reason << "\n";
            continue;
        }
        const steady_clock::time_point start = steady_clock::now();
        const EngineExecutionResult result = engine.execute(execution_request(circuit, obs));
        const auto elapsed_us = duration_cast<microseconds>(steady_clock::now() - start).count();

        std::cout << "   " << std::setw(5) << depth << std::setw(16)
                  << estimate.estimated_peak_bytes << std::setw(19) << result.actual_bytes
                  << std::setw(16) << estimate.estimated_work_units << std::setw(13) << elapsed_us
                  << "\n";
    }

    std::cout << "\n== 2. planning overhead on circuits that will never choose this engine ==\n";
    std::cout << "   the candidate is enumerated on every expectation job, so this is what it\n";
    std::cout << "   costs jobs it does not win\n";
    std::cout << "   n   depth  plan_job_us  winning_strategy\n";
    for (const std::uint32_t n : {8u, 12u, 16u, 20u}) {
        for (const std::uint32_t depth : {4u, 8u}) {
            const Circuit circuit = make_ordinary_circuit(n, depth);
            const Observable obs = z_on_every_qubit(n);
            const plan::JobSpec job_spec{.circuit = circuit,
                                         .shots = 0,
                                         .want_statevector = false,
                                         .seed = 1,
                                         .memory_budget_bytes = budget,
                                         .expectation = obs};
            const plan::HardwareProfile hw{.total_memory_bytes = budget * 2};

            const steady_clock::time_point start = steady_clock::now();
            const Plan plan = plan::plan_job(job_spec, hw);
            const auto elapsed_us =
                duration_cast<microseconds>(steady_clock::now() - start).count();

            std::cout << "   " << std::setw(3) << n << std::setw(7) << depth << std::setw(13)
                      << elapsed_us << "  " << static_cast<int>(plan.strategy) << "\n";
        }
    }

    return 0;
}
