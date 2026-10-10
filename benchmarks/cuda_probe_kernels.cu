#include <cuComplex.h>
#include <cuda_runtime.h>

#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <functional>

#include "cuda_probe_kernels.h"
#include "kernels/cuda/cuda_error.h"

namespace menos::benchmarks::cuda_probe {
namespace {

using kernels::cuda::ensure_cuda;

constexpr int kBlockSize = 256;

// The same sizes as src/kernels/cuda/dense_kernels.cu's symbols, so a copy
// here costs what a copy there costs.
constexpr int kMaxOperands = 5;
constexpr int kMaxMatrixEntries = 1 << (2 * kMaxOperands);
__constant__ cuDoubleComplex c_probe_matrix[kMaxMatrixEntries];
__constant__ std::uint32_t c_probe_operands[kMaxOperands];

// Inserts a zero bit at position `bit` of index.
__device__ inline std::size_t insert_zero(std::size_t index, std::uint32_t bit) {
    const std::size_t low = index & ((std::size_t{1} << bit) - 1);
    return ((index >> bit) << (bit + 1)) | low;
}

__global__ void h_kernel(cuDoubleComplex* __restrict__ state,
                         std::size_t pairs,
                         std::uint32_t qubit,
                         double scale) {
    const std::size_t i = static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (i >= pairs) {
        return;
    }
    const std::size_t lo = insert_zero(i, qubit);
    const std::size_t hi = lo | (std::size_t{1} << qubit);
    const cuDoubleComplex a = state[lo];
    const cuDoubleComplex b = state[hi];
    state[lo] = make_cuDoubleComplex((a.x + b.x) * scale, (a.y + b.y) * scale);
    state[hi] = make_cuDoubleComplex((a.x - b.x) * scale, (a.y - b.y) * scale);
}

__global__ void cx_kernel(cuDoubleComplex* __restrict__ state,
                          std::size_t groups,
                          std::uint32_t control,
                          std::uint32_t target) {
    const std::size_t i = static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (i >= groups) {
        return;
    }
    const std::uint32_t low_bit = control < target ? control : target;
    const std::uint32_t high_bit = control < target ? target : control;
    const std::size_t base =
        insert_zero(insert_zero(i, low_bit), high_bit) | (std::size_t{1} << control);
    const std::size_t flipped = base | (std::size_t{1} << target);
    const cuDoubleComplex a = state[base];
    state[base] = state[flipped];
    state[flipped] = a;
}

__global__ void empty_kernel() {}

__global__ void touch_kernel(cuDoubleComplex* __restrict__ state, std::size_t count, double one) {
    const std::size_t i = static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (i >= count) {
        return;
    }
    const cuDoubleComplex a = state[i];
    state[i] = make_cuDoubleComplex(a.x * one, a.y * one);
}

unsigned int grid_for(std::size_t items) {
    return static_cast<unsigned int>((items + kBlockSize - 1) / kBlockSize);
}

void launched(const char* what) {
    ensure_cuda(cudaGetLastError(), what);
    ensure_cuda(cudaDeviceSynchronize(), "cudaDeviceSynchronize");
}

}  // namespace

void apply_h(kernels::DeviceState state, std::uint32_t qubit) {
    const std::size_t pairs = state.num_amplitudes / 2;
    h_kernel<<<grid_for(pairs), kBlockSize>>>(
        static_cast<cuDoubleComplex*>(state.data), pairs, qubit, 1.0 / std::sqrt(2.0));
    launched("probe h_kernel launch");
}

void apply_cx(kernels::DeviceState state, std::uint32_t control, std::uint32_t target) {
    const std::size_t groups = state.num_amplitudes / 4;
    cx_kernel<<<grid_for(groups), kBlockSize>>>(
        static_cast<cuDoubleComplex*>(state.data), groups, control, target);
    launched("probe cx_kernel launch");
}

void empty_launch_and_sync() {
    empty_kernel<<<1, 1>>>();
    launched("empty_kernel launch");
}

void copy_gate_to_constant(const std::complex<double>* matrix,
                           std::size_t matrix_entries,
                           const std::uint32_t* operands,
                           std::size_t operand_count) {
    ensure_cuda(
        cudaMemcpyToSymbol(c_probe_matrix, matrix, matrix_entries * sizeof(std::complex<double>)),
        "cudaMemcpyToSymbol (probe matrix)");
    ensure_cuda(
        cudaMemcpyToSymbol(c_probe_operands, operands, operand_count * sizeof(std::uint32_t)),
        "cudaMemcpyToSymbol (probe operands)");
}

void touch_every_amplitude(kernels::DeviceState state) {
    touch_kernel<<<grid_for(state.num_amplitudes), kBlockSize>>>(
        static_cast<cuDoubleComplex*>(state.data), state.num_amplitudes, 1.0);
    launched("touch_kernel launch");
}

void copy_device_to_device(void* destination, const void* source, std::size_t bytes) {
    ensure_cuda(cudaMemcpy(destination, source, bytes, cudaMemcpyDeviceToDevice),
                "cudaMemcpy device-to-device");
    ensure_cuda(cudaDeviceSynchronize(), "cudaDeviceSynchronize");
}

double device_seconds(const std::function<void()>& work) {
    cudaEvent_t start = nullptr;
    cudaEvent_t stop = nullptr;
    ensure_cuda(cudaEventCreate(&start), "cudaEventCreate");
    ensure_cuda(cudaEventCreate(&stop), "cudaEventCreate");
    ensure_cuda(cudaEventRecord(start), "cudaEventRecord");
    work();
    ensure_cuda(cudaEventRecord(stop), "cudaEventRecord");
    ensure_cuda(cudaEventSynchronize(stop), "cudaEventSynchronize");
    float milliseconds = 0.0F;
    ensure_cuda(cudaEventElapsedTime(&milliseconds, start, stop), "cudaEventElapsedTime");
    static_cast<void>(cudaEventDestroy(start));
    static_cast<void>(cudaEventDestroy(stop));
    return static_cast<double>(milliseconds) / 1000.0;
}

}  // namespace menos::benchmarks::cuda_probe
