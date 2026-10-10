// Benchmark group 4 (docs/BUILD_PLAN.md Phase 4, "ADR Cross-engine cost
// units"): predicted-versus-measured work units for BOTH engines on the
// same all-Clifford circuits, forced explicitly through each engine
// (bypassing the planner, matching cost_model_correlation_benchmark.cpp's
// own methodology) -- a report, not a unit test (docs/adr/0006).

#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>

#include "engines/dense/dense_engine.h"
#include "engines/stabilizer/stabilizer_engine.h"
#include "menos/circuit.h"
#include "menos/plan.h"
#include "menos/simulation_engine.h"
#include "menos_benchmark_environment.h"
#include "plan/hardware_profile.h"
#include "plan/job_spec.h"
#include "plan/region_builder.h"
#include "runtime/runtime.h"

namespace {

using namespace std::chrono;
using namespace menos;

// All-Clifford brickwork H+CX -- feasible for both engines (no T/RX/etc),
// same circuit shape cost_model_correlation_benchmark.cpp uses for dense.
Circuit make_brickwork_circuit(std::uint32_t n) {
    Circuit circuit(n, static_cast<std::uint32_t>(n));
    for (QubitId q = 0; q < n; ++q) {
        circuit.append(Opcode::H, {q});
    }
    for (QubitId q = 0; q + 1 < n; q += 2) {
        circuit.append(Opcode::CX, {q, static_cast<QubitId>(q + 1)});
    }
    for (QubitId q = 0; q < n; ++q) {
        circuit.append_measure(q, q);
    }
    return circuit;
}

double measure_seconds(const Plan& forced_plan, const plan::JobSpec& job_spec, int repetitions) {
    const runtime::Runtime rt;
    static_cast<void>(rt.execute(forced_plan, job_spec));  // untimed warm-up
    const auto start = steady_clock::now();
    for (int i = 0; i < repetitions; ++i) {
        const Result result = rt.execute(forced_plan, job_spec);
        static_cast<void>(result.counts());
    }
    return duration<double>(steady_clock::now() - start).count() / repetitions;
}

}  // namespace

int main() {
    std::cout << menos_benchmark::describe_environment() << "\n";
    std::cout << "stabilizer-vs-dense cost correlation: work_units vs measured execution seconds, "
                 "same all-Clifford circuits, both engines forced explicitly\n";

    const engines::dense::DenseEngine dense_engine;
    const engines::stabilizer::StabilizerEngine stabilizer_engine;

    for (const std::uint32_t n : {std::uint32_t{4},
                                  std::uint32_t{8},
                                  std::uint32_t{12},
                                  std::uint32_t{16},
                                  std::uint32_t{20}}) {
        const Circuit circuit = make_brickwork_circuit(n);
        const plan::JobSpec job_spec{.circuit = circuit,
                                     .shots = 100,
                                     .want_statevector = false,
                                     .seed = 1,
                                     .memory_budget_bytes = std::nullopt,
                                     .expectation = std::nullopt};

        const EngineEstimateRequest estimate_request{
            .num_qubits = circuit.num_qubits(),
            .operations = &circuit.operations(),
            .want_statevector = false,
            .want_counts = true,
            .want_expectation = false,
            .observable = nullptr,
        };
        const CostEstimate dense_estimate = dense_engine.estimate(estimate_request);
        const CostEstimate stabilizer_estimate = stabilizer_engine.estimate(estimate_request);

        std::cout << "  n=" << n << "\n";
        if (dense_estimate.feasible) {
            Plan dense_plan;
            dense_plan.strategy = Strategy::DirectDense;
            dense_plan.exact = true;
            dense_plan.regions.push_back(plan::build_direct_region(
                circuit, Engine::Dense, Backend::Cpu, dense_estimate, nullptr));
            // This benchmark forces a plan directly, bypassing plan_job
            // (and its light-cone reduction) entirely -- circuit is
            // already what Runtime::execute() needs as the "reduced"
            // circuit, since no reduction ever ran.
            dense_plan.reduced_circuit = circuit;
            dense_plan.estimated_state_bytes = dense_estimate.estimated_peak_bytes;
            dense_plan.estimated_work_units = dense_estimate.estimated_work_units;
            const double t = measure_seconds(dense_plan, job_spec, /*repetitions=*/3);
            std::cout << "    dense:       work_units=" << dense_estimate.estimated_work_units
                      << " seconds=" << t << " ratio(work_units/seconds)="
                      << static_cast<double>(dense_estimate.estimated_work_units) / t << "\n";
        } else {
            std::cout << "    dense:       skipped (" << dense_estimate.infeasible_reason << ")\n";
        }

        if (stabilizer_estimate.feasible) {
            Plan stabilizer_plan;
            stabilizer_plan.strategy = Strategy::DirectStabilizer;
            stabilizer_plan.exact = true;
            stabilizer_plan.regions.push_back(plan::build_direct_region(
                circuit, Engine::Stabilizer, Backend::Cpu, stabilizer_estimate, nullptr));
            stabilizer_plan.reduced_circuit = circuit;
            stabilizer_plan.estimated_state_bytes = stabilizer_estimate.estimated_peak_bytes;
            stabilizer_plan.estimated_work_units = stabilizer_estimate.estimated_work_units;
            const double t = measure_seconds(stabilizer_plan, job_spec, /*repetitions=*/3);
            std::cout << "    stabilizer:  work_units=" << stabilizer_estimate.estimated_work_units
                      << " seconds=" << t << " ratio(work_units/seconds)="
                      << static_cast<double>(stabilizer_estimate.estimated_work_units) / t << "\n";
        } else {
            std::cout << "    stabilizer:  skipped (" << stabilizer_estimate.infeasible_reason
                      << ")\n";
        }
    }

    std::cout << "The two engines' work-unit ratios are not directly comparable to each other "
                 "(different operations on different hardware paths, docs/adr/0006) -- each "
                 "engine's own ratio is machine-specific evidence that its own cost model ranks "
                 "same-engine candidates usefully here, not a universal wall-clock guarantee.\n";
    return 0;
}
