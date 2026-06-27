// include/nam/bounded_int.hpp
//
// A tiny bounded-bit-width unsigned integer used by the automaton tier.
//
// The implementation plan calls for llvm::APInt; to keep Phase 1 buildable
// without a full LLVM dependency we vendor a minimal equivalent. The point
// that matters for the complexity-metric instrumentation is bit-width
// tracking: bit_width() must grow O(log n) for the n-th digit.
//
// When NAM_USE_LLVM_APINT is defined we alias to llvm::APInt instead.
#ifndef NAM_BOUNDED_INT_HPP
#define NAM_BOUNDED_INT_HPP

#include <cstdint>
#include <bit>

namespace nam {
    // A 128-bit unsigned integer is sufficient for the automaton tier of
    // Phase 1: remainders for rationals and degree-2 recurrence registers stay
    // well within this when emitting reasonable digit counts. We expose
    // bit_width() so tests can assert O(log n) growth shape.
    struct BoundedInt {
        // little-endian: lo is least significant.
        uint64_t lo{0};
        uint64_t hi{0};

        constexpr BoundedInt() = default;

        constexpr BoundedInt(uint64_t v) : lo(v), hi(0) {
        }

        constexpr BoundedInt(uint64_t h, uint64_t l) : lo(l), hi(h) {
        }

        constexpr bool is_zero() const { return lo == 0 && hi == 0; }

        constexpr int bit_width() const {
            if (hi != 0) return 128 - std::countl_zero(hi);
            if (lo != 0) return 64 - std::countl_zero(lo);
            return 0;
        }

        friend constexpr bool operator==(const BoundedInt &a, const BoundedInt &b) {
            return a.lo == b.lo && a.hi == b.hi;
        }

        friend constexpr bool operator<(const BoundedInt &a, const BoundedInt &b) {
            return (a.hi < b.hi) || (a.hi == b.hi && a.lo < b.lo);
        }

        friend constexpr bool operator<=(const BoundedInt &a, const BoundedInt &b) {
            return a < b || a == b;
        }

        friend constexpr BoundedInt operator+(BoundedInt a, const BoundedInt &b) {
            uint64_t lo = a.lo + b.lo;
            uint64_t carry = (lo < a.lo) ? 1u : 0u;
            uint64_t hi = a.hi + b.hi + carry;
            return BoundedInt(hi, lo);
        }

        friend constexpr BoundedInt operator-(BoundedInt a, const BoundedInt &b) {
            uint64_t lo = a.lo - b.lo;
            uint64_t borrow = (a.lo < b.lo) ? 1u : 0u;
            uint64_t hi = a.hi - b.hi - borrow;
            return BoundedInt(hi, lo);
        }

        // Multiply by a small (<= 2^32) factor; keeps within 128 bits for the
        // bounded use here. Suitable for "remainder * base" steps.
        constexpr BoundedInt mul_small(uint64_t f) const {
            // Split lo into 32-bit halves to avoid overflow in the partials.
            const uint64_t mask = 0xffffffffull;
            uint64_t l0 = lo & mask;
            uint64_t l1 = lo >> 32;
            uint64_t p0 = l0 * f;
            uint64_t p1 = l1 * f;
            uint64_t res_lo = p0 + (p1 << 32);
            uint64_t carry = (res_lo < p0) ? 1u : 0u;
            uint64_t res_hi = (p1 >> 32) + carry + hi * f;
            return BoundedInt(res_hi, res_lo);
        }

        // Long division by a 64-bit divisor. Returns quotient, sets remainder.
        constexpr BoundedInt divmod(uint64_t d, uint64_t &rem_out) const {
            // Restoring division over 128 bits. d fits in 64 bits.
            BoundedInt q;
            uint64_t rem = 0;
            for (int i = 127; i >= 0; --i) {
                rem = (rem << 1) | bit_at(i);
                if (rem >= d) {
                    rem -= d;
                    q.set_bit(i);
                }
            }
            rem_out = rem;
            return q;
        }

        constexpr uint64_t bit_at(int i) const {
            if (i >= 64) return (hi >> (i - 64)) & 1ull;
            return (lo >> i) & 1ull;
        }

        constexpr void set_bit(int i) {
            if (i >= 64) hi |= (1ull << (i - 64));
            else lo |= (1ull << i);
        }
    };
} // namespace nam

#endif // NAM_BOUNDED_INT_HPP
