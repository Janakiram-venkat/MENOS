#pragma once

#include <complex>
#include <optional>
#include <string>
#include <vector>

#include "menos/circuit.h"
#include "menos/observable.h"

namespace menos::testutil {

// A fixture circuit plus the statevector Qiskit computed for it
// (tools/gen_fixtures.py), and, since Phase 5, a random weighted Pauli
// observable plus its own Statevector.expectation_value oracle --
// present on every fixture circuit (tools/gen_fixtures.py always emits
// one), std::optional here only so a loader-level parsing failure has
// somewhere honest to leave it unset rather than a default-constructed
// Observable that would silently look valid.
struct FixtureCircuit {
    Circuit circuit;
    std::vector<std::complex<double>> expected_statevector;
    std::optional<Observable> observable;
    std::optional<std::complex<double>> expected_expectation;
    // OpenQASM 2 text for this circuit (tools/gen_fixtures.py's
    // qiskit.qasm2.dumps, Phase 7), absent for a circuit containing a
    // "unitary" operation -- qelib1.inc has no way to express one.
    std::optional<std::string> qasm2_text;
};

struct FixtureFile {
    std::string qiskit_version;
    std::vector<FixtureCircuit> circuits;
};

// Loads a fixture JSON file written by tools/gen_fixtures.py. Throws
// std::runtime_error naming the problem on a malformed file -- this is
// test-support code, not production KIR validation, so it does not use
// menos::common::Error.
[[nodiscard]] FixtureFile load_fixture_file(const std::string& path);

}  // namespace menos::testutil
