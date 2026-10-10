// What the Phase 11 execution-monitor seam costs (docs/adr/0015).
//
// The seam adds, per safe boundary, one branch on a null pointer when no
// monitor is installed, and additionally one virtual call plus one clock read
// when one is. Two claims are made about it in the ADR and in
// include/menos/simulation_engine.h, and this is what measures them:
//
//   1. A job that sets no time budget pays nothing that matters. This is the
//      important one, because it is every job written before Phase 11.
//   2. A monitored job's overhead is bounded by the per-boundary work of the
//      CHEAPEST engine. The stabilizer engine at a large qubit count is that
//      worst case, so it is measured directly rather than argued about.
//
// This is a benchmark, not a test (CLAUDE.md). It asserts nothing; it prints
// what it measured.

#include <benchmark/benchmark.h>

#include <chrono>
#include <cstdint>
#include <vector>

#include "engines/dense/dense_engine.h"
#include "engines/mps/mps_engine.h"
#include "engines/stabilizer/stabilizer_engine.h"
#include "menos/circuit.h"
#include "menos/simulation_engine.h"

namespace {

// Does the least a monitor can do: it never abandons, so what is measured is
// the seam itself rather than any policy built on it. It DOES read a clock,
// because that is what the real BudgetMonitor does and it is the dominant term.
class NullDecisionMonitor final : public menos::ExecutionMonitor {
public:
    void at_safe_boundary(std::uint64_t boundaries_crossed) const override {
        last_ = boundaries_crossed;
        observed_ = std::chrono::steady_clock::now().time_since_epoch().count();
    }

private:
    mutable std::uint64_t last_ = 0;
    mutable std::int64_t observed_ = 0;
};

// One aggregate rather than two adjacent same-width parameters, so each call
// below names its fields and a qubit count cannot be silently transposed with a
// layer count.
struct Shape {
    std::uint32_t num_qubits = 0;
    int layers = 0;
};

menos::Circuit clifford(Shape shape) {
    const std::uint32_t num_qubits = shape.num_qubits;
    const int layers = shape.layers;
    menos::Circuit circuit(num_qubits, num_qubits);
    for (int layer = 0; layer < layers; ++layer) {
        for (std::uint32_t q = 0; q < num_qubits; ++q) {
            circuit.append(menos::Opcode::H, {static_cast<menos::QubitId>(q)});
        }
        for (std::uint32_t q = static_cast<std::uint32_t>(layer % 2); q + 1 < num_qubits; q += 2) {
            circuit.append(menos::Opcode::CX,
                           {static_cast<menos::QubitId>(q), static_cast<menos::QubitId>(q + 1)});
        }
    }
    for (std::uint32_t q = 0; q < num_qubits; ++q) {
        circuit.append_measure(static_cast<menos::QubitId>(q), static_cast<menos::ClbitId>(q));
    }
    return circuit;
}

menos::Circuit non_clifford(Shape shape) {
    const std::uint32_t num_qubits = shape.num_qubits;
    const int layers = shape.layers;
    menos::Circuit circuit(num_qubits, num_qubits);
    for (int layer = 0; layer < layers; ++layer) {
        for (std::uint32_t q = 0; q < num_qubits; ++q) {
            circuit.append(menos::Opcode::H, {static_cast<menos::QubitId>(q)});
        }
        circuit.append(menos::Opcode::T, {0});
        for (std::uint32_t q = static_cast<std::uint32_t>(layer % 2); q + 1 < num_qubits; q += 2) {
            circuit.append(menos::Opcode::CX,
                           {static_cast<menos::QubitId>(q), static_cast<menos::QubitId>(q + 1)});
        }
    }
    for (std::uint32_t q = 0; q < num_qubits; ++q) {
        circuit.append_measure(static_cast<menos::QubitId>(q), static_cast<menos::ClbitId>(q));
    }
    return circuit;
}

menos::EngineExecutionRequest request_for(
    const menos::Circuit& circuit,
    std::uint64_t shots,
    const std::vector<std::pair<menos::QubitId, menos::ClbitId>>& targets,
    const menos::ExecutionMonitor* monitor) {
    return menos::EngineExecutionRequest{
        .num_qubits = circuit.num_qubits(),
        .operations = circuit.operations(),
        .want_statevector = false,
        .want_counts = shots > 0,
        .want_expectation = false,
        .shots = shots,
        .measure_targets = targets,
        .bitstring_width = circuit.num_clbits(),
        .seed = 7,
        .global_phase = 0.0,
        .budget_bytes = std::uint64_t{1} << 34,
        .is_sole_region = true,
        .observable = nullptr,
        .accuracy_policy = {},
        .monitor = monitor,
    };
}

std::vector<std::pair<menos::QubitId, menos::ClbitId>> targets_for(std::uint32_t num_qubits) {
    std::vector<std::pair<menos::QubitId, menos::ClbitId>> targets;
    for (std::uint32_t q = 0; q < num_qubits; ++q) {
        targets.emplace_back(static_cast<menos::QubitId>(q), static_cast<menos::ClbitId>(q));
    }
    return targets;
}

// The worst case for the seam: the cheapest per-boundary work in the codebase.
// 200 qubits of Clifford is roughly 1.26 us per gate, against a 31 ns clock
// read, so this is where the relative cost is largest.
void BM_StabilizerUnmonitored(benchmark::State& state) {
    const menos::engines::stabilizer::StabilizerEngine engine;
    const menos::Circuit circuit = clifford({.num_qubits = 200, .layers = 40});
    const auto targets = targets_for(200);
    for (auto _ : state) {
        benchmark::DoNotOptimize(engine.execute(request_for(circuit, 1024, targets, nullptr)));
    }
}
BENCHMARK(BM_StabilizerUnmonitored)->Unit(benchmark::kMillisecond);

void BM_StabilizerMonitored(benchmark::State& state) {
    const menos::engines::stabilizer::StabilizerEngine engine;
    const menos::Circuit circuit = clifford({.num_qubits = 200, .layers = 40});
    const auto targets = targets_for(200);
    const NullDecisionMonitor monitor;
    for (auto _ : state) {
        benchmark::DoNotOptimize(engine.execute(request_for(circuit, 1024, targets, &monitor)));
    }
}
BENCHMARK(BM_StabilizerMonitored)->Unit(benchmark::kMillisecond);

// The realistic case: an engine whose per-boundary work is large. Here the
// seam should be unmeasurable.
void BM_DenseUnmonitored(benchmark::State& state) {
    const menos::engines::dense::DenseEngine engine;
    const menos::Circuit circuit = non_clifford({.num_qubits = 18, .layers = 8});
    const auto targets = targets_for(18);
    for (auto _ : state) {
        benchmark::DoNotOptimize(engine.execute(request_for(circuit, 1024, targets, nullptr)));
    }
}
BENCHMARK(BM_DenseUnmonitored)->Unit(benchmark::kMillisecond);

void BM_DenseMonitored(benchmark::State& state) {
    const menos::engines::dense::DenseEngine engine;
    const menos::Circuit circuit = non_clifford({.num_qubits = 18, .layers = 8});
    const auto targets = targets_for(18);
    const NullDecisionMonitor monitor;
    for (auto _ : state) {
        benchmark::DoNotOptimize(engine.execute(request_for(circuit, 1024, targets, &monitor)));
    }
}
BENCHMARK(BM_DenseMonitored)->Unit(benchmark::kMillisecond);

// MPS, where 69% of the time is counts sampling -- the phase of the work that
// Phase 11 had to extend monitoring into, and therefore the one whose boundary
// count is dominated by shots rather than gates.
void BM_MpsUnmonitored(benchmark::State& state) {
    const menos::engines::mps::MPSEngine engine;
    const menos::Circuit circuit = non_clifford({.num_qubits = 16, .layers = 8});
    const auto targets = targets_for(16);
    for (auto _ : state) {
        benchmark::DoNotOptimize(engine.execute(request_for(circuit, 1024, targets, nullptr)));
    }
}
BENCHMARK(BM_MpsUnmonitored)->Unit(benchmark::kMillisecond);

void BM_MpsMonitored(benchmark::State& state) {
    const menos::engines::mps::MPSEngine engine;
    const menos::Circuit circuit = non_clifford({.num_qubits = 16, .layers = 8});
    const auto targets = targets_for(16);
    const NullDecisionMonitor monitor;
    for (auto _ : state) {
        benchmark::DoNotOptimize(engine.execute(request_for(circuit, 1024, targets, &monitor)));
    }
}
BENCHMARK(BM_MpsMonitored)->Unit(benchmark::kMillisecond);

}  // namespace
