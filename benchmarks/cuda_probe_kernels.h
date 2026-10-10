#pragma once

// Benchmark-only device code for Phase 16 step 1 (docs/adr/0020 section 2).
// Nothing here is linked into a MENOS library, and nothing here is a
// specialised kernel MENOS uses: the probe kernels re-create the hand-written
// H and CX kernels of docs/adr/0014 section 16.3 so that the gap between them
// and the shipped generic kernel can be measured again, and the remaining
// functions time the parts of a gate's cost separately.
//
// Like src/kernels/dense_kernels.h, this header names no CUDA type, so the
// benchmark's .cpp is ordinary host C++.

#include <complex>
#include <cstddef>
#include <cstdint>
#include <functional>

#include "kernels/dense_kernels.h"

namespace menos::benchmarks::cuda_probe {

// The probe's H on qubit q: one thread per amplitude pair, (a + b) / sqrt(2)
// and (a - b) / sqrt(2). Launches and synchronizes, as the generic
// apply_matrix does per gate. Not bitwise identical to the generic kernel
// (it computes a different expression); the benchmark reports the
// difference.
void apply_h(kernels::DeviceState state, std::uint32_t qubit);

// The probe's CX(control, target): one thread per group of four, swapping
// the two amplitudes whose control bit is 1. Launches and synchronizes.
void apply_cx(kernels::DeviceState state, std::uint32_t control, std::uint32_t target);

// An empty kernel, launched with one block and synchronized: the fixed cost of
// one launch plus one cudaDeviceSynchronize.
void empty_launch_and_sync();

// Two cudaMemcpyToSymbol calls into this file's own __constant__ symbols, of
// matrix_entries complex values and operand_count operands: the same copies
// kernels::cuda::apply_matrix makes before every launch.
void copy_gate_to_constant(const std::complex<double>* matrix,
                           std::size_t matrix_entries,
                           const std::uint32_t* operands,
                           std::size_t operand_count);

// Reads and writes every amplitude once (multiplied by a run-time 1.0, so the
// pass cannot be elided): a gate's memory traffic with no arithmetic.
// Launches and synchronizes.
void touch_every_amplitude(kernels::DeviceState state);

// cudaMemcpy device-to-device of bytes, then synchronize.
void copy_device_to_device(void* destination, const void* source, std::size_t bytes);

// Device time of `work` in seconds, from CUDA events recorded on the default
// stream immediately before and after it.
double device_seconds(const std::function<void()>& work);

}  // namespace menos::benchmarks::cuda_probe
