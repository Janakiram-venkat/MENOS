#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <new>
#include <span>
#include <vector>

namespace menos::engines::dense {

inline constexpr std::size_t kStateVectorAlignment = 64;

// A 64-byte-aligned allocator, so amplitude buffers meet CLAUDE.md's
// alignment convention for SIMD-friendliness in later phases.
template <typename T>
struct AlignedAllocator {
    using value_type = T;

    AlignedAllocator() noexcept = default;
    template <typename U>
    explicit AlignedAllocator(const AlignedAllocator<U>&) noexcept {}

    [[nodiscard]] T* allocate(std::size_t n) {
        if (n == 0) {
            return nullptr;
        }
        return static_cast<T*>(
            ::operator new(n * sizeof(T), std::align_val_t{kStateVectorAlignment}));
    }

    void deallocate(T* p, std::size_t) noexcept {
        ::operator delete(p, std::align_val_t{kStateVectorAlignment});
    }
};

template <typename T, typename U>
bool operator==(const AlignedAllocator<T>&, const AlignedAllocator<U>&) noexcept {
    return true;
}

template <typename T, typename U>
bool operator!=(const AlignedAllocator<T>&, const AlignedAllocator<U>&) noexcept {
    return false;
}

// A 64-byte-aligned complex128 statevector of num_qubits qubits. Sized
// through checked_pow2 and budget-checked before any allocation: throws
// ResourceExceeded (naming the required and available bytes) rather than
// letting an allocation throw std::bad_alloc.
class StateVector {
public:
    StateVector(int num_qubits, std::uint64_t budget_bytes);

    [[nodiscard]] int num_qubits() const;
    [[nodiscard]] std::uint64_t size_bytes() const;
    [[nodiscard]] std::span<std::complex<double>> amplitudes();
    [[nodiscard]] std::span<const std::complex<double>> amplitudes() const;

private:
    int num_qubits_;
    std::vector<std::complex<double>, AlignedAllocator<std::complex<double>>> data_;
};

// Sets state to exp(i * global_phase) * |0...0>. Precondition: state's
// buffer is freshly constructed (all zero).
void initialize(StateVector& state, double global_phase);

}  // namespace menos::engines::dense
