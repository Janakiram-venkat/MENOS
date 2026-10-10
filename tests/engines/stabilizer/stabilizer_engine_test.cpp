#include "engines/stabilizer/stabilizer_engine.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <map>
#include <random>
#include <set>

#include "engines/dense/dense_engine.h"
#include "engines/stabilizer/tableau.h"

namespace menos::engines::stabilizer {
namespace {

using engines::dense::DenseEngine;
using MeasureTarget = std::pair<QubitId, ClbitId>;

CostEstimate stabilizer_estimate(const StabilizerEngine& engine,
                                 std::uint32_t num_qubits,
                                 const std::vector<Operation>& ops,
                                 bool want_statevector,
                                 bool want_counts) {
    const EngineEstimateRequest request{
        .num_qubits = num_qubits,
        .operations = &ops,
        .want_statevector = want_statevector,
        .want_counts = want_counts,
        .want_expectation = false,
        .observable = nullptr,
    };
    return engine.estimate(request);
}

TEST(StabilizerEngineTest, NameAndCapabilities) {
    const StabilizerEngine engine;
    EXPECT_EQ(engine.name(), "stabilizer");
    EXPECT_EQ(engine.engine_id(), Engine::Stabilizer);
    const EngineCapabilities caps = engine.capabilities();
    EXPECT_TRUE(caps.counts);
    EXPECT_FALSE(caps.statevector);
    EXPECT_TRUE(caps.expectation);
}

TEST(StabilizerEngineTest, StatevectorRequestIsInfeasibleNamingDense) {
    const StabilizerEngine engine;
    const CostEstimate estimate =
        stabilizer_estimate(engine, 2, {}, /*want_statevector=*/true, false);
    EXPECT_FALSE(estimate.feasible);
    EXPECT_NE(estimate.infeasible_reason.find("dense"), std::string::npos);
}

TEST(StabilizerEngineTest, NonCliffordOperationIsInfeasibleNamingIndexAndGate) {
    const StabilizerEngine engine;
    const std::vector<Operation> ops{
        GateOp{Opcode::H, {0}, {}},
        GateOp{Opcode::T, {0}, {}},
    };
    const CostEstimate estimate = stabilizer_estimate(engine, 1, ops, false, true);
    EXPECT_FALSE(estimate.feasible);
    EXPECT_NE(estimate.infeasible_reason.find("operation 1"), std::string::npos);
    EXPECT_NE(estimate.infeasible_reason.find('t'), std::string::npos);
}

TEST(StabilizerEngineTest, FiveHundredQubitCliffordCircuitIsFeasible) {
    // The whole point of the stabilizer engine: a scale dense cannot
    // even represent (16 * 2^500 bytes) is trivially cheap here, because
    // the cost formula is polynomial in num_qubits, never checked_pow2.
    Circuit circuit(500, 500);
    for (QubitId q = 0; q < 500; ++q) {
        circuit.append(Opcode::H, {q});
    }
    for (QubitId q = 0; q + 1 < 500; q += 2) {
        circuit.append(Opcode::CX, {q, static_cast<QubitId>(q + 1)});
    }
    for (QubitId q = 0; q < 500; ++q) {
        circuit.append_measure(q, q);
    }
    const StabilizerEngine engine;
    const CostEstimate estimate = stabilizer_estimate(
        engine, 500, circuit.operations(), /*want_statevector=*/false, /*want_counts=*/true);
    ASSERT_TRUE(estimate.feasible);
    EXPECT_LT(estimate.estimated_peak_bytes, 1'000'000U)
        << "the tableau for 500 qubits must be kilobytes, not 16*2^500 bytes";
}

TEST(StabilizerEngineTest, SameSeedGivesIdenticalCountsAcrossRuns) {
    const std::vector<Operation> ops{
        GateOp{Opcode::H, {0}, {}},
        GateOp{Opcode::CX, {0, 1}, {}},
    };
    const std::vector<MeasureTarget> targets{{0, 0}, {1, 1}};
    const StabilizerEngine engine;
    const EngineExecutionRequest request{
        .num_qubits = 2,
        .operations = ops,
        .want_statevector = false,
        .want_counts = true,
        .want_expectation = false,
        .shots = 2000,
        .measure_targets = targets,
        .bitstring_width = 2,
        .seed = 2024,
        .global_phase = 0.0,
        .budget_bytes = 1'000'000,
        .is_sole_region = true,
        .observable = nullptr,
    };
    const EngineExecutionResult a = engine.execute(request);
    const EngineExecutionResult b = engine.execute(request);
    ASSERT_TRUE(a.counts.has_value());
    ASSERT_TRUE(b.counts.has_value());
    // ASSERT_TRUE above already guarantees this; clang-tidy can't see
    // through the gtest macro.
    EXPECT_EQ(*a.counts, *b.counts);  // NOLINT(bugprone-unchecked-optional-access)
}

TEST(StabilizerEngineTest, BellCircuitCountsAreOnlyCorrelatedOutcomes) {
    const std::vector<Operation> ops{
        GateOp{Opcode::H, {0}, {}},
        GateOp{Opcode::CX, {0, 1}, {}},
    };
    const std::vector<MeasureTarget> targets{{0, 0}, {1, 1}};
    const StabilizerEngine engine;
    const EngineExecutionRequest request{
        .num_qubits = 2,
        .operations = ops,
        .want_statevector = false,
        .want_counts = true,
        .want_expectation = false,
        .shots = 1000,
        .measure_targets = targets,
        .bitstring_width = 2,
        .seed = 42,
        .global_phase = 0.0,
        .budget_bytes = 1'000'000,
        .is_sole_region = true,
        .observable = nullptr,
    };
    const EngineExecutionResult result = engine.execute(request);
    ASSERT_TRUE(result.counts.has_value());
    std::uint64_t total = 0;
    // ASSERT_TRUE above already guarantees this; clang-tidy can't see
    // through the gtest macro.
    for (const auto& [key, count] : *result.counts) {  // NOLINT(bugprone-unchecked-optional-access)
        EXPECT_EQ(key.test(0), key.test(1));
        total += count;
    }
    EXPECT_EQ(total, 1000U);
}

// --- Random Clifford differential test against the dense oracle
// (docs/BUILD_PLAN.md Phase 4 acceptance criterion 1) ---

BitString project_outcome(const std::vector<bool>& full,
                          const std::vector<MeasureTarget>& targets,
                          std::uint32_t width) {
    BitString bs(width);
    for (const auto& [qubit, clbit] : targets) {
        if (full[qubit]) {
            bs.set(clbit, true);
        }
    }
    return bs;
}

// All 2^generators.size() reachable full-width outcomes, projected onto
// targets and deduplicated -- feasible since generators.size() <= 12
// (at most 4096 subsets) for every circuit this test generates.
std::set<BitString> reachable_projected_outcomes(const std::vector<bool>& base,
                                                 const std::vector<std::vector<bool>>& generators,
                                                 const std::vector<MeasureTarget>& targets,
                                                 std::uint32_t width) {
    std::set<BitString> outcomes;
    const std::size_t r = generators.size();
    const std::uint64_t subsets = std::uint64_t{1} << r;
    for (std::uint64_t mask = 0; mask < subsets; ++mask) {
        std::vector<bool> full = base;
        for (std::size_t g = 0; g < r; ++g) {
            if (((mask >> g) & 1U) != 0) {
                for (std::size_t q = 0; q < full.size(); ++q) {
                    full[q] = full[q] != generators[g][q];
                }
            }
        }
        outcomes.insert(project_outcome(full, targets, width));
    }
    return outcomes;
}

// Exact Born-rule probability of each projected outcome, from the dense
// statevector (the independent oracle every other engine is validated
// against, docs/BUILD_PLAN.md Phase 4 acceptance criterion 1: "compare
// against the dense oracle").
std::map<BitString, double> dense_probabilities(const Circuit& circuit,
                                                const std::vector<MeasureTarget>& targets,
                                                std::uint32_t width) {
    const DenseEngine dense;
    const EngineExecutionRequest request{
        .num_qubits = circuit.num_qubits(),
        .operations = circuit.operations(),
        .want_statevector = true,
        .want_counts = false,
        .want_expectation = false,
        .shots = 0,
        .measure_targets = targets,
        .bitstring_width = width,
        .seed = 0,
        .global_phase = circuit.global_phase,
        .budget_bytes = 1ULL << 30,
        .is_sole_region = true,
        .observable = nullptr,
    };
    const EngineExecutionResult result = dense.execute(request);
    // want_statevector is fixed true above, and DenseEngine::execute()
    // always populates it in that case; clang-tidy can't see that.
    const std::vector<std::complex<double>>& amplitudes =
        result.statevector.value();  // NOLINT(bugprone-unchecked-optional-access)
    std::map<BitString, double> probabilities;
    for (std::size_t basis = 0; basis < amplitudes.size(); ++basis) {
        const double p = std::norm(amplitudes[basis]);
        if (p == 0.0) {
            continue;
        }
        BitString outcome(width);
        for (const auto& [qubit, clbit] : targets) {
            if ((basis >> qubit) & std::size_t{1}) {
                outcome.set(clbit, true);
            }
        }
        probabilities[outcome] += p;
    }
    return probabilities;
}

std::map<ClbitId, QubitId> last_writes(const std::vector<Operation>& operations) {
    std::map<ClbitId, QubitId> writes;
    for (const Operation& op : operations) {
        if (const auto* measure = std::get_if<MeasureOp>(&op)) {
            writes[measure->clbit] = measure->qubit;
        }
    }
    return writes;
}

std::uint32_t random_below(std::mt19937_64& rng, std::uint32_t bound) {
    return static_cast<std::uint32_t>(rng() % bound);
}

// A random Clifford circuit over the full Clifford set (docs/BUILD_PLAN.md
// Phase 4: "the full Clifford set"), with a subset of active qubits
// (leaving the rest genuinely idle) and a subset of measured qubits
// (some clbits reused so a later measurement overwrites an earlier
// one -- "repeated writes to one classical bit").
// seed (an arbitrary 64-bit RNG seed) and n (a qubit count, 1-12 in this
// test) are not realistically confusable at any call site.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
Circuit make_random_clifford_circuit(std::uint64_t seed, std::uint32_t n) {
    std::mt19937_64 rng(seed);
    Circuit circuit(n, n);

    const std::uint32_t num_active = std::max<std::uint32_t>(1, n - random_below(rng, n / 2 + 1));
    std::vector<QubitId> active(num_active);
    for (std::uint32_t i = 0; i < num_active; ++i) {
        active[i] = i;
    }

    const std::array<Opcode, 6> one_qubit{
        Opcode::X, Opcode::Y, Opcode::Z, Opcode::H, Opcode::S, Opcode::Sdg};
    const std::array<Opcode, 5> one_qubit_more{
        Opcode::SX, Opcode::SXdg, Opcode::X, Opcode::Y, Opcode::Z};
    const std::array<Opcode, 3> two_qubit{Opcode::CX, Opcode::CZ, Opcode::SWAP};

    const std::uint32_t num_gates = 5 + random_below(rng, 20);
    for (std::uint32_t i = 0; i < num_gates; ++i) {
        if (num_active >= 2 && random_below(rng, 2) == 0) {
            const QubitId a = active[random_below(rng, num_active)];
            QubitId b = active[random_below(rng, num_active)];
            while (b == a) {
                b = active[random_below(rng, num_active)];
            }
            circuit.append(
                two_qubit[random_below(rng, static_cast<std::uint32_t>(two_qubit.size()))], {a, b});
        } else {
            const QubitId a = active[random_below(rng, num_active)];
            const Opcode op =
                (random_below(rng, 2) == 0)
                    ? one_qubit[random_below(rng, static_cast<std::uint32_t>(one_qubit.size()))]
                    : one_qubit_more[random_below(
                          rng, static_cast<std::uint32_t>(one_qubit_more.size()))];
            circuit.append(op, {a});
        }
    }

    // Measure every qubit (active or idle) to clbit == qubit index, then
    // sometimes overwrite an earlier clbit from a later qubit
    // ("repeated writes"), exactly mirroring CLAUDE.md's last-write-wins
    // invariant.
    for (QubitId q = 0; q < n; ++q) {
        circuit.append_measure(q, q);
    }
    if (n >= 2 && random_below(rng, 3) == 0) {
        const ClbitId reused_clbit = random_below(rng, n);
        const QubitId overwriting_qubit = random_below(rng, n);
        circuit.append_measure(overwriting_qubit, reused_clbit);
    }

    return circuit;
}

TEST(StabilizerEngineDifferentialTest, MatchesDenseOracleAcrossRandomCliffordCircuits) {
    constexpr std::uint64_t kSeed = 20260922;
    std::mt19937_64 seed_source(kSeed);
    const StabilizerEngine stabilizer;

    for (std::uint32_t n = 1; n <= 12; ++n) {
        for (int trial = 0; trial < 3; ++trial) {
            const std::uint64_t circuit_seed = seed_source();
            const Circuit circuit = make_random_clifford_circuit(circuit_seed, n);

            const std::map<ClbitId, QubitId> writes = last_writes(circuit.operations());
            std::vector<MeasureTarget> targets;
            targets.reserve(writes.size());
            for (const auto& [clbit, qubit] : writes) {
                targets.emplace_back(qubit, clbit);
            }

            Tableau tableau(n);
            for (const Operation& op : circuit.operations()) {
                if (const auto* gate = std::get_if<GateOp>(&op)) {
                    switch (gate->opcode) {
                        case Opcode::X:
                            tableau.apply_x(gate->qubits[0]);
                            break;
                        case Opcode::Y:
                            tableau.apply_y(gate->qubits[0]);
                            break;
                        case Opcode::Z:
                            tableau.apply_z(gate->qubits[0]);
                            break;
                        case Opcode::H:
                            tableau.apply_h(gate->qubits[0]);
                            break;
                        case Opcode::S:
                            tableau.apply_s(gate->qubits[0]);
                            break;
                        case Opcode::Sdg:
                            tableau.apply_sdg(gate->qubits[0]);
                            break;
                        case Opcode::SX:
                            tableau.apply_sx(gate->qubits[0]);
                            break;
                        case Opcode::SXdg:
                            tableau.apply_sxdg(gate->qubits[0]);
                            break;
                        case Opcode::CX:
                            tableau.apply_cx(gate->qubits[0], gate->qubits[1]);
                            break;
                        case Opcode::CZ:
                            tableau.apply_cz(gate->qubits[0], gate->qubits[1]);
                            break;
                        case Opcode::SWAP:
                            tableau.apply_swap(gate->qubits[0], gate->qubits[1]);
                            break;
                        default:
                            FAIL() << "generator produced a non-Clifford opcode; seed="
                                   << circuit_seed;
                    }
                }
            }

            const Tableau::AffineSubspace subspace = tableau.reduce();
            const std::set<BitString> reachable =
                reachable_projected_outcomes(subspace.base, subspace.generators, targets, n);
            const auto r_observed =
                static_cast<int>(std::round(std::log2(static_cast<double>(reachable.size()))));
            ASSERT_EQ(std::uint64_t{1} << r_observed, reachable.size())
                << "reachable-outcome count must be an exact power of two; seed=" << circuit_seed;

            const std::map<BitString, double> dense_probs =
                dense_probabilities(circuit, targets, n);

            std::set<BitString> dense_nonzero;
            for (const auto& [key, p] : dense_probs) {
                if (p > 1e-9) {
                    dense_nonzero.insert(key);
                }
            }

            EXPECT_EQ(dense_nonzero, reachable)
                << "seed=" << circuit_seed << " n=" << n
                << ": stabilizer's reachable outcome set must equal dense's nonzero outcome set";

            const double expected_p = std::pow(2.0, -r_observed);
            for (const BitString& outcome : reachable) {
                const auto it = dense_probs.find(outcome);
                ASSERT_NE(it, dense_probs.end()) << "seed=" << circuit_seed;
                EXPECT_NEAR(it->second, expected_p, 1e-9) << "seed=" << circuit_seed << " n=" << n
                                                          << ": every reachable outcome must have "
                                                             "probability exactly 2^-r";
            }
        }
    }
}

}  // namespace
}  // namespace menos::engines::stabilizer
