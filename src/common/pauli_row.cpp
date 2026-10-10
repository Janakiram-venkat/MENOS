#include "common/pauli_row.h"

#include "common/error.h"

namespace menos::common {

bool get_bit(const std::vector<std::uint64_t>& words, std::uint32_t q) {
    return ((words[q / 64] >> (q % 64)) & std::uint64_t{1}) != 0;
}

void set_bit(std::vector<std::uint64_t>& words, std::uint32_t q, bool value) {
    if (value) {
        words[q / 64] |= (std::uint64_t{1} << (q % 64));
    } else {
        words[q / 64] &= ~(std::uint64_t{1} << (q % 64));
    }
}

void xor_bit(std::vector<std::uint64_t>& words, std::uint32_t q, bool value) {
    if (value) {
        words[q / 64] ^= (std::uint64_t{1} << (q % 64));
    }
}

void apply_x(PauliRow& row, std::uint32_t a) {
    if (get_bit(row.z, a)) {
        row.r = !row.r;
    }
}

void apply_z(PauliRow& row, std::uint32_t a) {
    if (get_bit(row.x, a)) {
        row.r = !row.r;
    }
}

void apply_y(PauliRow& row, std::uint32_t a) {
    if (get_bit(row.x, a) != get_bit(row.z, a)) {
        row.r = !row.r;
    }
}

void apply_h(PauliRow& row, std::uint32_t a) {
    const bool xa = get_bit(row.x, a);
    const bool za = get_bit(row.z, a);
    if (xa && za) {
        row.r = !row.r;
    }
    set_bit(row.x, a, za);
    set_bit(row.z, a, xa);
}

void apply_s(PauliRow& row, std::uint32_t a) {
    const bool xa = get_bit(row.x, a);
    const bool za = get_bit(row.z, a);
    if (xa && za) {
        row.r = !row.r;
    }
    xor_bit(row.z, a, xa);
}

void apply_sdg(PauliRow& row, std::uint32_t a) {
    const bool xa = get_bit(row.x, a);
    const bool za = get_bit(row.z, a);
    if (xa && !za) {
        row.r = !row.r;
    }
    xor_bit(row.z, a, xa);
}

void apply_sx(PauliRow& row, std::uint32_t a) {
    const bool xa = get_bit(row.x, a);
    const bool za = get_bit(row.z, a);
    if (!xa && za) {
        row.r = !row.r;
    }
    xor_bit(row.x, a, za);
}

void apply_sxdg(PauliRow& row, std::uint32_t a) {
    const bool xa = get_bit(row.x, a);
    const bool za = get_bit(row.z, a);
    if (xa && za) {
        row.r = !row.r;
    }
    xor_bit(row.x, a, za);
}

void apply_cx(PauliRow& row, std::uint32_t control, std::uint32_t target) {
    ensure(control != target, "apply_cx: control and target must differ");
    const bool xa = get_bit(row.x, control);
    const bool za = get_bit(row.z, control);
    const bool xb = get_bit(row.x, target);
    const bool zb = get_bit(row.z, target);
    if (xa && zb && (xb == za)) {
        row.r = !row.r;
    }
    xor_bit(row.x, target, xa);
    xor_bit(row.z, control, zb);
}

void apply_cz(PauliRow& row, std::uint32_t a, std::uint32_t b) {
    ensure(a != b, "apply_cz: the two qubits must differ");
    const bool xa = get_bit(row.x, a);
    const bool za = get_bit(row.z, a);
    const bool xb = get_bit(row.x, b);
    const bool zb = get_bit(row.z, b);
    if (xa && xb && (za != zb)) {
        row.r = !row.r;
    }
    xor_bit(row.z, a, xb);
    xor_bit(row.z, b, xa);
}

void apply_swap(PauliRow& row, std::uint32_t a, std::uint32_t b) {
    ensure(a != b, "apply_swap: the two qubits must differ");
    const bool xa = get_bit(row.x, a);
    const bool za = get_bit(row.z, a);
    const bool xb = get_bit(row.x, b);
    const bool zb = get_bit(row.z, b);
    set_bit(row.x, a, xb);
    set_bit(row.x, b, xa);
    set_bit(row.z, a, zb);
    set_bit(row.z, b, za);
}

// sigma1*sigma2 = i^g * sigma(x1^x2, z1^z2), derived and tabulated in
// docs/adr/0006 by direct 2x2 matrix multiplication for every
// (sigma1, sigma2) pair.
int pauli_phase_exponent(bool x1, bool z1, bool x2, bool z2) {
    if ((!x1 && !z1) || (!x2 && !z2)) {
        return 0;  // either operand is I
    }
    if (x1 == x2 && z1 == z2) {
        return 0;  // same nontrivial type: X*X, Y*Y, Z*Z
    }
    if (x1 && !z1) {  // sigma1 = X, sigma2 in {Z, Y}
        return x2 ? 1 : 3;
    }
    if (!x1 && z1) {  // sigma1 = Z, sigma2 in {X, Y}
        return z2 ? 3 : 1;
    }
    // sigma1 = Y, sigma2 in {X, Z}
    return x2 ? 3 : 1;
}

std::array<std::complex<double>, 4> pauli_matrix_2x2(bool x, bool z) {
    using Complex = std::complex<double>;
    if (!x && !z) {
        return {Complex{1.0, 0.0}, Complex{0.0, 0.0}, Complex{0.0, 0.0}, Complex{1.0, 0.0}};
    }
    if (x && !z) {
        return {Complex{0.0, 0.0}, Complex{1.0, 0.0}, Complex{1.0, 0.0}, Complex{0.0, 0.0}};
    }
    if (!x && z) {
        return {Complex{1.0, 0.0}, Complex{0.0, 0.0}, Complex{0.0, 0.0}, Complex{-1.0, 0.0}};
    }
    return {Complex{0.0, 0.0}, Complex{0.0, -1.0}, Complex{0.0, 1.0}, Complex{0.0, 0.0}};
}

}  // namespace menos::common
