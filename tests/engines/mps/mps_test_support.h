#pragma once

#include <complex>
#include <vector>

#include "engines/mps/mps_state.h"

namespace menos::engines::mps {

// Full statevector reconstruction, for differential testing only (the
// engine's own capabilities() never advertises Statevector --
// docs/BUILD_PLAN.md Phase 6 -- so this exists solely so tests can
// compare against the dense oracle; no production code calls it).
// Amplitude index bit j is qubit j's value (site i is always qubit i),
// matching CLAUDE.md's little-endian convention.
inline std::vector<std::complex<double>> reconstruct_statevector(const MPSState& state) {
    using Complex = std::complex<double>;
    const std::uint32_t n = state.num_qubits();

    // running[combo * chi + r]: amplitude for the bit-pattern combo of
    // the qubits processed so far, still carrying an open right bond
    // index r into the next unprocessed site.
    std::vector<Complex> running = {Complex{1.0, 0.0}};
    std::size_t chi = 1;

    for (std::uint32_t i = 0; i < n; ++i) {
        const SiteTensor& site = state.site(i);
        const std::size_t combos = std::size_t{1} << i;
        std::vector<Complex> next(combos * 2 * site.chi_right, Complex{0.0, 0.0});
        for (std::size_t combo = 0; combo < combos; ++combo) {
            for (std::size_t s = 0; s < 2; ++s) {
                for (std::size_t r = 0; r < site.chi_right; ++r) {
                    Complex sum{0.0, 0.0};
                    for (std::size_t l = 0; l < chi; ++l) {
                        sum += running[combo * chi + l] * site.at(l, s, r);
                    }
                    next[(combo + (s << i)) * site.chi_right + r] = sum;
                }
            }
        }
        running = std::move(next);
        chi = site.chi_right;
    }

    return running;  // chi == 1 here (the last site's own chi_right).
}

}  // namespace menos::engines::mps
