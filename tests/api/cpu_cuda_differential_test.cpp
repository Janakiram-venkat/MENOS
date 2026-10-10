#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <random>
#include <string>
#include <vector>

#include "common/tolerance.h"
#include "engines/dense/dense_engine.h"
#include "engines/dense_cuda/cuda_dense_engine.h"
#include "menos/circuit.h"
#include "menos/observable.h"
#include "menos/simulation_engine.h"

namespace menos {
namespace {

using engines::dense::DenseEngine;
using engines::dense_cuda::CudaDenseEngine;

// labels[q] is the Pauli acting on qubit q -- index IS qubit number, matching
// the little-endian convention, deliberately not the highest-qubit-first
// string order a human would write. Built here rather than with a library
// helper because none exists; spelling it out keeps the bit convention under
// the test's own control. Y sets both the x and z bit, which is the
// Y = i*X*Z convention CLAUDE.md requires.
PauliString make_pauli(std::uint32_t num_qubits, const std::string& labels) {
    PauliString pauli;
    pauli.num_qubits = num_qubits;
    pauli.x_words.assign(1, 0);
    pauli.z_words.assign(1, 0);
    for (std::uint32_t q = 0; q < labels.size(); ++q) {
        const std::uint64_t bit = std::uint64_t{1} << q;
        switch (labels[q]) {
            case 'X':
                pauli.x_words[0] |= bit;
                break;
            case 'Z':
                pauli.z_words[0] |= bit;
                break;
            case 'Y':
                pauli.x_words[0] |= bit;
                pauli.z_words[0] |= bit;
                break;
            default:
                break;  // 'I'
        }
    }
    return pauli;
}

// The CPU dense engine is the oracle, exactly as it is for every other engine
// in this project (docs/adr/0014). A CUDA result is never accepted because it
// looks physically plausible; it is accepted because it matches the CPU.
//
// Two different bars are used, deliberately:
//
//   STATEVECTOR and COUNTS are required to be BITWISE IDENTICAL, not merely
//   close. The device kernels are compiled with -fmad=false so their arithmetic
//   is operation-for-operation the same as the CPU's, and the device
//   apply_matrix sums its 2^k products in the same order. Counts are then
//   sampled host-side from that identical state by the same sampler, so
//   CLAUDE.md's determinism invariant ("the same circuit, options and seed
//   produce the same counts") holds across backends by construction. Asserting
//   exact equality is what keeps it that way: a tolerance here would let a
//   future change silently break it.
//
//   EXPECTATION is compared within common::kDefaultAbsTol. Its device
//   reduction is a block tree-sum, a different summation order from the CPU's
//   sequential loop, so bitwise equality is not a property either backend has
//   or should promise. The tolerance is the existing project constant; nothing
//   was loosened for CUDA.

class CpuCudaDifferentialTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!engines::dense_cuda::cuda_device_available()) {
            GTEST_SKIP() << "no CUDA device available -- CPU/CUDA differential coverage did NOT "
                            "run in this configuration";
        }
    }

    const DenseEngine cpu_engine;
    const CudaDenseEngine cuda_engine;

    static EngineExecutionRequest request_for(const Circuit& circuit,
                                              bool want_statevector,
                                              bool want_expectation,
                                              const Observable* observable,
                                              double global_phase = 0.0) {
        return EngineExecutionRequest{
            .num_qubits = circuit.num_qubits(),
            .operations = circuit.operations(),
            .want_statevector = want_statevector,
            .want_counts = false,
            .want_expectation = want_expectation,
            .shots = 0,
            .measure_targets = {},
            .bitstring_width = 0,
            .seed = 0,
            .global_phase = global_phase,
            .budget_bytes = std::uint64_t{1} << 32,
            .is_sole_region = true,
            .observable = observable,
        };
    }

    // Asserts bitwise equality, element by element, so a failure names the
    // index rather than just a norm.
    void expect_statevectors_identical(const Circuit& circuit, double global_phase = 0.0) {
        const EngineExecutionRequest request =
            request_for(circuit, /*want_statevector=*/true, false, nullptr, global_phase);
        const EngineExecutionResult cpu = cpu_engine.execute(request);
        const EngineExecutionResult gpu = cuda_engine.execute(request);
        ASSERT_TRUE(cpu.statevector.has_value());
        ASSERT_TRUE(gpu.statevector.has_value());
        ASSERT_EQ(cpu.statevector->size(), gpu.statevector->size());
        for (std::size_t i = 0; i < cpu.statevector->size(); ++i) {
            const std::complex<double> a = (*cpu.statevector)[i];
            const std::complex<double> b = (*gpu.statevector)[i];
            ASSERT_EQ(a.real(), b.real()) << "real part differs at amplitude " << i;
            ASSERT_EQ(a.imag(), b.imag()) << "imag part differs at amplitude " << i;
        }
    }
};

// --- single-qubit gates ----------------------------------------------------

TEST_F(CpuCudaDifferentialTest, SingleQubitGatesOnEveryQubitMatchExactly) {
    for (const Opcode opcode : {Opcode::H, Opcode::X, Opcode::Y, Opcode::Z, Opcode::S, Opcode::T}) {
        constexpr std::uint32_t kQubits = 6;
        Circuit circuit(kQubits, 0);
        // Spread the state first so a gate acting on any qubit is observable.
        for (std::uint32_t q = 0; q < kQubits; ++q) {
            circuit.append(Opcode::H, {static_cast<QubitId>(q)});
        }
        for (std::uint32_t q = 0; q < kQubits; ++q) {
            circuit.append(opcode, {static_cast<QubitId>(q)});
        }
        SCOPED_TRACE("opcode index " + std::to_string(static_cast<int>(opcode)));
        expect_statevectors_identical(circuit);
    }
}

// RZ and P differ by a global phase, and MENOS treats the statevector as an
// output, so the two must stay distinguishable on the device as well.
TEST_F(CpuCudaDifferentialTest, RzAndPhaseAreDistinguishableAndBothMatchTheCpu) {
    constexpr double kAngle = 0.7;
    Circuit rz(2, 0);
    rz.append(Opcode::H, {0});
    rz.append(Opcode::RZ, {0}, {kAngle});
    Circuit phase(2, 0);
    phase.append(Opcode::H, {0});
    phase.append(Opcode::P, {0}, {kAngle});

    expect_statevectors_identical(rz);
    expect_statevectors_identical(phase);

    const EngineExecutionResult rz_gpu = cuda_engine.execute(request_for(rz, true, false, nullptr));
    const EngineExecutionResult p_gpu =
        cuda_engine.execute(request_for(phase, true, false, nullptr));
    ASSERT_TRUE(rz_gpu.statevector.has_value());
    ASSERT_TRUE(p_gpu.statevector.has_value());
    EXPECT_NE((*rz_gpu.statevector)[0], (*p_gpu.statevector)[0])
        << "RZ and P must not collapse to the same state on the device either";
}

TEST_F(CpuCudaDifferentialTest, NonTrivialGlobalPhaseMatchesExactly) {
    for (const double phase : {0.0, 0.7, -2.3, std::numbers::pi}) {
        Circuit circuit(3, 0);
        circuit.append(Opcode::H, {0});
        circuit.append(Opcode::CX, {0, 1});
        SCOPED_TRACE("global_phase " + std::to_string(phase));
        expect_statevectors_identical(circuit, phase);
    }
}

// --- multi-qubit gates and operand order -----------------------------------

TEST_F(CpuCudaDifferentialTest, TwoQubitGatesMatchInBothOperandOrders) {
    for (const Opcode opcode : {Opcode::CX, Opcode::CZ, Opcode::SWAP}) {
        for (const bool reversed : {false, true}) {
            Circuit circuit(4, 0);
            for (std::uint32_t q = 0; q < 4; ++q) {
                circuit.append(Opcode::H, {static_cast<QubitId>(q)});
            }
            circuit.append(Opcode::T, {1});  // break the symmetry between operands
            if (reversed) {
                circuit.append(opcode, {2, 0});
            } else {
                circuit.append(opcode, {0, 2});
            }
            SCOPED_TRACE("opcode " + std::to_string(static_cast<int>(opcode)) +
                         (reversed ? " reversed" : " forward"));
            expect_statevectors_identical(circuit);
        }
    }
}

TEST_F(CpuCudaDifferentialTest, ThreeQubitGateMatchesExactly) {
    Circuit circuit(4, 0);
    for (std::uint32_t q = 0; q < 4; ++q) {
        circuit.append(Opcode::H, {static_cast<QubitId>(q)});
    }
    circuit.append(Opcode::T, {2});
    circuit.append(Opcode::CCX, {0, 1, 3});
    expect_statevectors_identical(circuit);
}

// --- parameterized gates ---------------------------------------------------

TEST_F(CpuCudaDifferentialTest, ParameterizedRotationsAtIrrationalAnglesMatchExactly) {
    Circuit circuit(5, 0);
    const double a = std::numbers::pi / 7.0;
    const double b = std::sqrt(2.0);
    const double c = std::numbers::e / 3.0;
    for (std::uint32_t q = 0; q < 5; ++q) {
        circuit.append(Opcode::H, {static_cast<QubitId>(q)});
    }
    circuit.append(Opcode::RX, {0}, {a});
    circuit.append(Opcode::RY, {1}, {b});
    circuit.append(Opcode::RZ, {2}, {c});
    circuit.append(Opcode::RX, {3}, {-a});
    circuit.append(Opcode::RY, {4}, {b * 2.0});
    expect_statevectors_identical(circuit);
}

// --- the generic k-operand path, via UnitaryOp -----------------------------

TEST_F(CpuCudaDifferentialTest, UnitaryOperandsUpToTheIrCeilingMatchExactly) {
    // kMaxUnitaryQubits is the widest operand list the IR admits, and the
    // device kernel's own ceiling is asserted equal to it, so this walks the
    // generic path to its documented limit.
    std::mt19937_64 rng(20260929);
    for (int k = 1; k <= kMaxUnitaryQubits; ++k) {
        const std::uint32_t num_qubits = static_cast<std::uint32_t>(k) + 1;
        const std::size_t dim = std::size_t{1} << k;

        // A genuine unitary: the discrete Fourier transform matrix, which is
        // unitary for every size and is not a permutation, so it exercises real
        // complex arithmetic rather than index shuffling.
        std::vector<std::complex<double>> matrix(dim * dim);
        const double norm = 1.0 / std::sqrt(static_cast<double>(dim));
        for (std::size_t r = 0; r < dim; ++r) {
            for (std::size_t c = 0; c < dim; ++c) {
                const double angle =
                    2.0 * std::numbers::pi * static_cast<double>(r * c) / static_cast<double>(dim);
                matrix[r * dim + c] = norm * std::complex<double>{std::cos(angle), std::sin(angle)};
            }
        }

        std::vector<QubitId> operands(num_qubits);
        for (std::uint32_t q = 0; q < num_qubits; ++q) {
            operands[q] = static_cast<QubitId>(q);
        }
        std::shuffle(operands.begin(), operands.end(), rng);
        operands.resize(static_cast<std::size_t>(k));

        Circuit circuit(num_qubits, 0);
        for (std::uint32_t q = 0; q < num_qubits; ++q) {
            circuit.append(Opcode::H, {static_cast<QubitId>(q)});
        }
        circuit.append(Opcode::T, {0});
        circuit.append_unitary(operands, matrix);

        SCOPED_TRACE("k = " + std::to_string(k));
        expect_statevectors_identical(circuit);
    }
}

// --- randomized circuits ---------------------------------------------------

TEST_F(CpuCudaDifferentialTest, RandomCircuitsMatchExactlyAcrossQubitCounts) {
    constexpr std::uint64_t kSeed = 987654321;
    std::mt19937_64 rng(kSeed);
    for (std::uint32_t n = 4; n <= 12; ++n) {
        Circuit circuit(n, 0);
        for (int layer = 0; layer < 6; ++layer) {
            for (std::uint32_t q = 0; q < n; ++q) {
                const int choice = static_cast<int>(rng() % 5);
                const double angle = static_cast<double>(rng() % 1000) / 500.0;
                switch (choice) {
                    case 0:
                        circuit.append(Opcode::H, {static_cast<QubitId>(q)});
                        break;
                    case 1:
                        circuit.append(Opcode::T, {static_cast<QubitId>(q)});
                        break;
                    case 2:
                        circuit.append(Opcode::RX, {static_cast<QubitId>(q)}, {angle});
                        break;
                    case 3:
                        circuit.append(Opcode::RY, {static_cast<QubitId>(q)}, {angle});
                        break;
                    default:
                        circuit.append(Opcode::RZ, {static_cast<QubitId>(q)}, {angle});
                        break;
                }
            }
            for (std::uint32_t q = static_cast<std::uint32_t>(layer % 2); q + 1 < n; q += 2) {
                circuit.append(Opcode::CX, {static_cast<QubitId>(q), static_cast<QubitId>(q + 1)});
            }
        }
        SCOPED_TRACE("n = " + std::to_string(n) + ", seed = " + std::to_string(kSeed));
        expect_statevectors_identical(circuit);
    }
}

// --- counts ----------------------------------------------------------------

TEST_F(CpuCudaDifferentialTest, CountsAreIdenticalForTheSameSeed) {
    constexpr std::uint32_t kQubits = 6;
    Circuit circuit(kQubits, kQubits);
    for (std::uint32_t q = 0; q < kQubits; ++q) {
        circuit.append(Opcode::H, {static_cast<QubitId>(q)});
    }
    circuit.append(Opcode::T, {0});
    for (std::uint32_t q = 0; q + 1 < kQubits; q += 2) {
        circuit.append(Opcode::CX, {static_cast<QubitId>(q), static_cast<QubitId>(q + 1)});
    }

    std::vector<std::pair<QubitId, ClbitId>> targets;
    for (std::uint32_t q = 0; q < kQubits; ++q) {
        targets.emplace_back(static_cast<QubitId>(q), static_cast<ClbitId>(q));
    }

    const EngineExecutionRequest request{
        .num_qubits = kQubits,
        .operations = circuit.operations(),
        .want_statevector = false,
        .want_counts = true,
        .want_expectation = false,
        .shots = 4096,
        .measure_targets = targets,
        .bitstring_width = kQubits,
        .seed = 424242,
        .global_phase = 0.0,
        .budget_bytes = std::uint64_t{1} << 32,
        .is_sole_region = true,
        .observable = nullptr,
    };

    const EngineExecutionResult cpu = cpu_engine.execute(request);
    const EngineExecutionResult gpu = cuda_engine.execute(request);
    ASSERT_TRUE(cpu.counts.has_value());
    ASSERT_TRUE(gpu.counts.has_value());
    // Not "statistically compatible" -- identical. See the header comment.
    EXPECT_EQ(*cpu.counts, *gpu.counts);
}

// --- expectation -----------------------------------------------------------

TEST_F(CpuCudaDifferentialTest, MultiTermExpectationMatchesWithinTolerance) {
    constexpr std::uint32_t kQubits = 8;
    Circuit circuit(kQubits, 0);
    for (std::uint32_t q = 0; q < kQubits; ++q) {
        circuit.append(Opcode::H, {static_cast<QubitId>(q)});
    }
    circuit.append(Opcode::T, {1});
    circuit.append(Opcode::RY, {3}, {0.61});
    for (std::uint32_t q = 0; q + 1 < kQubits; q += 2) {
        circuit.append(Opcode::CX, {static_cast<QubitId>(q), static_cast<QubitId>(q + 1)});
    }

    // Includes Y terms, so the Y = i*X*Z phase convention is exercised on the
    // device rather than only X/Z, where the prefactor would be trivial.
    Observable observable;
    observable.terms.emplace_back(make_pauli(kQubits, "ZIIIIIII"), std::complex<double>{1.0, 0.0});
    observable.terms.emplace_back(make_pauli(kQubits, "IXIIIIII"), std::complex<double>{-0.5, 0.0});
    observable.terms.emplace_back(make_pauli(kQubits, "IIYIIIII"), std::complex<double>{0.25, 0.0});
    observable.terms.emplace_back(make_pauli(kQubits, "YYIIIIII"),
                                  std::complex<double>{0.125, 0.0});
    observable.terms.emplace_back(make_pauli(kQubits, "ZXYIIIII"),
                                  std::complex<double>{-0.75, 0.0});

    const EngineExecutionRequest request = request_for(circuit, false, true, &observable);
    const EngineExecutionResult cpu = cpu_engine.execute(request);
    const EngineExecutionResult gpu = cuda_engine.execute(request);
    ASSERT_TRUE(cpu.expectation_terms.has_value());
    ASSERT_TRUE(gpu.expectation_terms.has_value());
    ASSERT_EQ(cpu.expectation_terms->size(), gpu.expectation_terms->size());
    for (std::size_t i = 0; i < cpu.expectation_terms->size(); ++i) {
        EXPECT_NEAR((*cpu.expectation_terms)[i].real(),
                    (*gpu.expectation_terms)[i].real(),
                    common::kDefaultAbsTol)
            << "term " << i;
        EXPECT_NEAR((*cpu.expectation_terms)[i].imag(),
                    (*gpu.expectation_terms)[i].imag(),
                    common::kDefaultAbsTol)
            << "term " << i;
    }
}

// --- device memory refusal -------------------------------------------------

// The 4 GB card caps the dense state near n = 27. Past that the backend must
// refuse with a typed, budget-named error before any cudaMalloc, never let an
// out-of-memory failure escape from the driver (CLAUDE.md: failure throws
// ResourceExceeded, never std::bad_alloc -- the device analogue).
TEST_F(CpuCudaDifferentialTest, AnOversizedRegionIsRefusedByEstimateNotByTheDriver) {
    constexpr std::uint32_t kTooManyQubits = 30;  // 16 GiB, far beyond this GPU
    const Circuit circuit(kTooManyQubits, 0);
    const EngineEstimateRequest estimate_request{
        kTooManyQubits,
        &circuit.operations(),
        /*want_statevector=*/true,
        /*want_counts=*/false,
        /*want_expectation=*/false,
        /*observable=*/nullptr,
    };
    const CostEstimate estimate = cuda_engine.estimate(estimate_request);
    EXPECT_FALSE(estimate.feasible);
    EXPECT_NE(estimate.infeasible_reason.find("device memory"), std::string::npos)
        << "the reason must name device memory: " << estimate.infeasible_reason;
}

}  // namespace
}  // namespace menos
