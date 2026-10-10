// Phase 13 (docs/adr/0017 section 8): the two measurements the ADR requires
// before the gate.
//
// 1. Does the calibrated CPU work-unit rate, measured on the dense engine,
//    predict the trajectory engine's runtime? The engine runs the same
//    dense::apply_matrix kernel, but as a loop of N short runs, which may be
//    overhead-dominated at small n. For noisy brickwork circuits it forces a
//    DirectTrajectory plan through the runtime (bypassing the planner, the
//    methodology of the other correlation benchmarks) and prints predicted
//    seconds, measured seconds and their ratio, across n and N.
//
// 2. How pessimistic is the trajectory count? N_eps uses Var(X_t) <= C^2. This
//    prints the measured per-trajectory variance (se^2 * N) divided by C^2 for
//    the same circuits with a sum-of-Z observable.
//
// A report, not a unit test.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iostream>
#include <optional>
#include <vector>

#include "cost/trajectory_count.h"
#include "engines/trajectory/trajectory_engine.h"
#include "menos/circuit.h"
#include "menos/observable.h"
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

std::vector<std::vector<Complex>> amplitude_damping(double gamma) {
    return {{1.0, 0.0, 0.0, std::sqrt(1.0 - gamma)}, {0.0, std::sqrt(gamma), 0.0, 0.0}};
}

// Two layers of H, RZ and CX brickwork, a depolarizing and an amplitude-damping
// channel on every qubit after each layer, then (optionally) every qubit
// measured -- the density-matrix benchmark's circuit, with a state-dependent
// channel added.
Circuit make_noisy_brickwork(std::uint32_t n, bool measured) {
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
            circuit.append_channel({q}, depolarizing_1q(0.02));
            circuit.append_channel({q}, amplitude_damping(0.05));
        }
    }
    if (measured) {
        for (QubitId q = 0; q < n; ++q) {
            circuit.append_measure(q, q);
        }
    }
    return circuit;
}

Observable sum_of_z(std::uint32_t n) {
    Observable observable;
    for (std::uint32_t q = 0; q < n; ++q) {
        observable.terms.emplace_back(PauliString{n, {0}, {std::uint64_t{1} << q}},
                                      Complex{1.0, 0.0});
    }
    return observable;
}

Plan forced_trajectory_plan(const Circuit& circuit,
                            const CostEstimate& estimate,
                            const Observable* observable,
                            const cost::TrajectoryCount& count) {
    Plan forced;
    forced.strategy = Strategy::DirectTrajectory;
    forced.exact = false;
    forced.regions.push_back(
        plan::build_direct_region(circuit, Engine::Trajectory, Backend::Cpu, estimate, observable));
    forced.reduced_circuit = circuit;
    forced.estimated_state_bytes = estimate.estimated_peak_bytes;
    forced.estimated_work_units = estimate.estimated_work_units;
    forced.trajectory_summary = TrajectorySummary{.trajectories = count.trajectories,
                                                  .precision = std::nullopt,
                                                  .coefficient_bound = count.coefficient_bound};
    return forced;
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
    const engines::trajectory::TrajectoryEngine engine;

    std::cout << "trajectory cost correlation: predicted (calibrated dense work-unit rate "
              << profile.work_units_per_second << " /s, bandwidth "
              << profile.memory_bandwidth_bytes_per_second
              << " B/s) vs measured execution seconds, DirectTrajectory forced, counts with one "
                 "shot per trajectory\n";
    for (const std::uint32_t n : {4U, 8U, 12U, 16U, 20U}) {
        for (const std::uint64_t trajectories : {std::uint64_t{100}, std::uint64_t{1000}}) {
            if (n >= 20 && trajectories > 100) {
                continue;  // minutes per repetition; the n = 20, N = 100 row suffices
            }
            const Circuit circuit = make_noisy_brickwork(n, /*measured=*/true);
            const cost::TrajectoryCount count{trajectories, std::nullopt};
            const CostEstimate estimate =
                engine.estimate(EngineEstimateRequest{.num_qubits = n,
                                                      .operations = &circuit.operations(),
                                                      .want_statevector = false,
                                                      .want_counts = true,
                                                      .want_expectation = false,
                                                      .observable = nullptr,
                                                      .trajectories = trajectories});
            const plan::JobSpec job_spec{.circuit = circuit,
                                         .shots = trajectories,
                                         .want_statevector = false,
                                         .seed = 1,
                                         .memory_budget_bytes = std::nullopt,
                                         .expectation = std::nullopt};
            const Plan forced = forced_trajectory_plan(circuit, estimate, nullptr, count);
            const double predicted = std::max(
                static_cast<double>(estimate.estimated_work_units) / profile.work_units_per_second,
                static_cast<double>(estimate.estimated_peak_bytes) /
                    profile.memory_bandwidth_bytes_per_second);
            const int repetitions = n <= 12 ? 5 : 1;
            const double measured = measure_seconds(forced, job_spec, repetitions);
            std::cout << "  n=" << n << " N=" << trajectories
                      << " operations=" << circuit.operations().size()
                      << " work_units=" << estimate.estimated_work_units
                      << " predicted_s=" << predicted << " measured_s=" << measured
                      << " measured/predicted=" << measured / predicted << "\n";
        }
    }

    std::cout << "\nvariance bound: measured per-trajectory variance of X = sum_i Z_i (se^2 * N) "
                 "against the bound C^2 = n^2 that N_eps assumes\n";
    for (const std::uint32_t n : {4U, 8U, 12U}) {
        const Circuit circuit = make_noisy_brickwork(n, /*measured=*/false);
        const Observable observable = sum_of_z(n);
        constexpr std::uint64_t kTrajectories = 2000;
        const cost::TrajectoryCount count{kTrajectories, static_cast<double>(n)};
        const CostEstimate estimate =
            engine.estimate(EngineEstimateRequest{.num_qubits = n,
                                                  .operations = &circuit.operations(),
                                                  .want_statevector = false,
                                                  .want_counts = false,
                                                  .want_expectation = true,
                                                  .observable = &observable,
                                                  .trajectories = kTrajectories});
        const plan::JobSpec job_spec{.circuit = circuit,
                                     .shots = 0,
                                     .want_statevector = false,
                                     .seed = 3,
                                     .memory_budget_bytes = std::nullopt,
                                     .expectation = observable};
        const runtime::Runtime rt;
        const Result result =
            rt.execute(forced_trajectory_plan(circuit, estimate, &observable, count), job_spec);
        const double se = result.expectation_standard_error().value_or(0.0);
        const double variance = se * se * static_cast<double>(kTrajectories);
        const double bound = static_cast<double>(n) * static_cast<double>(n);
        std::cout << "  n=" << n << " N=" << kTrajectories << " variance=" << variance
                  << " C^2=" << bound << " variance/C^2=" << variance / bound
                  << " (N could have been " << variance / bound
                  << " of N_eps for the same true error)\n";
    }
    std::cout << "A measured/predicted ratio near 1 means the planner's seconds for this engine "
                 "are trustworthy. A variance/C^2 ratio well below 1 means N_eps is conservative: "
                 "safe, but more trajectories than the precision needs.\n";
    return 0;
}
