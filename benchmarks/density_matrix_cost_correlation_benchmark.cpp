// Phase 12 (docs/adr/0016 section 5): does the calibrated CPU work-unit rate,
// measured on the dense engine, predict the density-matrix engine's runtime?
// The engine runs the same dense::apply_matrix kernel, on a 2n-qubit vector,
// so the claim is plausible but is a claim; this program measures it. For
// noisy brickwork circuits it forces a DirectDensityMatrix plan through the
// runtime (bypassing the planner, the same methodology as the other
// correlation benchmarks) and prints predicted seconds (work units / the
// calibrated rate, and the bandwidth floor), measured seconds, and their ratio.
// A report, not a unit test.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iostream>
#include <vector>

#include "engines/density_matrix/density_matrix_engine.h"
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
using Complex = std::complex<double>;

std::vector<std::vector<Complex>> depolarizing_1q(double p) {
    const double a = std::sqrt(1.0 - 3.0 * p / 4.0);
    const double b = std::sqrt(p / 4.0);
    return {{a, 0.0, 0.0, a},
            {0.0, b, b, 0.0},
            {0.0, Complex{0.0, -b}, Complex{0.0, b}, 0.0},
            {b, 0.0, 0.0, -b}};
}

// Two layers of H, RZ and CX brickwork, a depolarizing channel on every qubit
// after each layer, then every qubit measured.
Circuit make_noisy_brickwork(std::uint32_t n) {
    Circuit circuit(n, n);
    for (std::uint32_t layer = 0; layer < 2; ++layer) {
        for (QubitId q = 0; q < n; ++q) {
            circuit.append(Opcode::H, {q});
            circuit.append(Opcode::RZ, {q}, {0.1 * static_cast<double>(q + 1)});
        }
        for (QubitId q = layer % 2; q + 1 < n; q += 2) {
            circuit.append(Opcode::CX, {q, static_cast<QubitId>(q + 1)});
        }
        for (QubitId q = 0; q < n; ++q) {
            circuit.append_channel({q}, depolarizing_1q(0.01));
        }
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
    const plan::HardwareProfile& profile = plan::calibrated_hardware_profile();
    std::cout << "density-matrix cost correlation: predicted (calibrated dense work-unit rate "
              << profile.work_units_per_second << " /s, bandwidth "
              << profile.memory_bandwidth_bytes_per_second
              << " B/s) vs measured execution seconds, DirectDensityMatrix forced\n";

    const engines::density_matrix::DensityMatrixEngine engine;
    for (const std::uint32_t n : {std::uint32_t{4},
                                  std::uint32_t{6},
                                  std::uint32_t{8},
                                  std::uint32_t{10},
                                  std::uint32_t{11},
                                  std::uint32_t{12}}) {
        const Circuit circuit = make_noisy_brickwork(n);
        const plan::JobSpec job_spec{.circuit = circuit,
                                     .shots = 1000,
                                     .want_statevector = false,
                                     .seed = 1,
                                     .memory_budget_bytes = std::nullopt,
                                     .expectation = std::nullopt};
        const CostEstimate estimate =
            engine.estimate(EngineEstimateRequest{.num_qubits = n,
                                                  .operations = &circuit.operations(),
                                                  .want_statevector = false,
                                                  .want_counts = true,
                                                  .want_expectation = false,
                                                  .observable = nullptr});
        if (!estimate.feasible) {
            std::cout << "  n=" << n << " skipped (" << estimate.infeasible_reason << ")\n";
            continue;
        }
        Plan forced;
        forced.strategy = Strategy::DirectDensityMatrix;
        forced.exact = true;
        forced.regions.push_back(plan::build_direct_region(
            circuit, Engine::DensityMatrix, Backend::Cpu, estimate, nullptr));
        forced.reduced_circuit = circuit;
        forced.estimated_state_bytes = estimate.estimated_peak_bytes;
        forced.estimated_work_units = estimate.estimated_work_units;

        const double predicted = std::max(
            static_cast<double>(estimate.estimated_work_units) / profile.work_units_per_second,
            static_cast<double>(estimate.estimated_peak_bytes) /
                profile.memory_bandwidth_bytes_per_second);
        const int repetitions = n <= 8 ? 20 : 3;
        const double measured = measure_seconds(forced, job_spec, repetitions);
        std::cout << "  n=" << n << " operations=" << circuit.operations().size()
                  << " work_units=" << estimate.estimated_work_units
                  << " peak_bytes=" << estimate.estimated_peak_bytes << " predicted_s=" << predicted
                  << " measured_s=" << measured << " measured/predicted=" << measured / predicted
                  << "\n";
    }
    std::cout << "A measured/predicted ratio near 1 means the planner's seconds for this engine "
                 "(time budgets, the CPU/CUDA comparison) are trustworthy; a large ratio means "
                 "they are optimistic, as the MPS time model is (docs/adr/0015).\n";
    return 0;
}
