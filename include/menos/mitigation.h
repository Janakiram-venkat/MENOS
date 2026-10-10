#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "menos/bitstring.h"
#include "menos/circuit.h"
#include "menos/menos.h"
#include "menos/result.h"

namespace menos {

// Readout-error mitigation (Phase 14, docs/adr/0018). Explicit opt-in: nothing
// in run(), run_batch() or the Qiskit adapters mitigates on its own.
//
// The method is tensored assignment-matrix inversion. Two calibration circuits
// are run beside the target: one measures every measured qubit from |0>, the
// other after an X on each. Their counts estimate each measured clbit's 2x2
// readout matrix A_c, and the target's empirical distribution p over its m
// measured clbits is mapped to the quasi-distribution
//
//     q = (A_1^-1 (x) ... (x) A_m^-1) p,
//
// computed exactly over all 2^m outcomes. MENOS's readout errors are
// independent per measurement (correlated ones are refused), so this tensored
// model is the exact form of the readout channel. Only the matrices are
// estimated.

// calibration_shots (K) has no default: the uncertainty of the result depends
// on it, so the caller chooses it. Must be > 0.
struct ReadoutMitigationOptions {
    std::uint64_t calibration_shots;
};

// The estimated readout error of one measured clbit.
struct ClbitCalibration {
    ClbitId clbit;
    double p1_given_0;  // e0: P(record 1 | true 0)
    double p0_given_1;  // e1: P(record 0 | true 1)
};

// A mitigated distribution. Never exact: q is an estimate whose error is
// reported per outcome.
//
// The dense vectors are indexed over the measured clbits: bit k of index i is
// the value of measured_clbits()[k], in ascending clbit order. outcome(i) gives
// the full classical bit string, with every unmeasured clbit 0, as in counts.
class MitigatedResult {
public:
    // NOLINTBEGIN(bugprone-easily-swappable-parameters)
    MitigatedResult(std::vector<ClbitId> measured_clbits,
                    std::uint32_t num_clbits,
                    std::vector<double> quasi_probabilities,
                    std::vector<double> target_standard_errors,
                    std::vector<double> standard_errors,
                    double gamma,
                    std::vector<ClbitCalibration> calibration,
                    std::uint64_t calibration_shots,
                    Result raw,
                    std::vector<Result> calibration_results);
    // NOLINTEND(bugprone-easily-swappable-parameters)

    [[nodiscard]] const std::vector<ClbitId>& measured_clbits() const;
    [[nodiscard]] BitString outcome(std::size_t index) const;
    // q, over all 2^m outcomes. Sums to 1 up to rounding; may be negative.
    [[nodiscard]] const std::vector<double>& quasi_probabilities() const;
    // The standard error of each q_i: sqrt(target variance + calibration
    // variance). The target-shot-noise part is the exact multinomial variance
    // with the empirical distribution substituted; the calibration part is a
    // first-order delta-method approximation (docs/adr/0018 section 4).
    [[nodiscard]] const std::vector<double>& standard_errors() const;
    // The target-shot-noise part alone, for callers who want to separate them.
    [[nodiscard]] const std::vector<double>& target_standard_errors() const;
    // prod_c ||A_c^-1||_1. For an observable with eigenvalues in [-1, 1], the
    // target-shot-noise standard error of an expectation computed from q is at
    // most gamma / sqrt(N).
    [[nodiscard]] double gamma() const;
    [[nodiscard]] const std::vector<ClbitCalibration>& calibration() const;
    [[nodiscard]] std::uint64_t calibration_shots() const;
    // The unmitigated target run, with its plan, counts and explain().
    [[nodiscard]] const Result& raw() const;
    // The all-zero and all-one calibration runs, in that order.
    [[nodiscard]] const std::vector<Result>& calibration_results() const;
    // Always false.
    [[nodiscard]] bool exact() const;
    // raw().explain() followed by a readout-mitigation section.
    [[nodiscard]] std::string explain() const;

private:
    std::vector<ClbitId> measured_clbits_;
    std::uint32_t num_clbits_;
    std::vector<double> quasi_probabilities_;
    std::vector<double> target_standard_errors_;
    std::vector<double> standard_errors_;
    double gamma_;
    std::vector<ClbitCalibration> calibration_;
    std::uint64_t calibration_shots_;
    Result raw_;
    std::vector<Result> calibration_results_;
};

// The calibration pair for target with noiseless preparation: the all-zero and
// all-one circuits, measuring each of target's surviving (qubit, clbit) pairs
// with that measurement's own readout error. A caller who has a gate-noise
// model (the Qiskit adapter) builds its own pair under that model instead
// (docs/adr/0018 section 9, Q1). Throws InvalidCircuit if target measures
// nothing.
[[nodiscard]] std::array<Circuit, 2> readout_calibration_circuits(const Circuit& target);

// Runs target and its calibration pair as one batch (run_batch's seed rule:
// target, zero and one take the first, second and third derived seeds),
// estimates the readout matrices and returns the mitigated distribution.
//
// The target runs under options as given; options.shots (N) must be > 0. The
// calibration circuits run with calibration_shots and request counts only.
// calibration == nullopt uses readout_calibration_circuits(target). A supplied
// pair must have target's qubit and clbit counts and measure exactly target's
// surviving (qubit, clbit) pairs; MENOS cannot check what it prepares.
//
// Throws InvalidCircuit for a target with no measurement, shots == 0,
// calibration_shots == 0 or a mismatched pair; ResourceExceeded, before any
// circuit runs, when the dense mitigation vectors exceed the memory budget; and
// UnsupportedFeature when an estimated readout is no better than a coin flip.
[[nodiscard]] MitigatedResult run_readout_mitigated(
    const Circuit& target,
    const std::optional<std::array<Circuit, 2>>& calibration,
    const RunOptions& options,
    const ReadoutMitigationOptions& mitigation);

}  // namespace menos
