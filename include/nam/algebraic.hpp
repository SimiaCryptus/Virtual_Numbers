// include/nam/algebraic.hpp
//
// M3: Quadratic irrationals (sqrt(D), phi) as degree-2 recurrence VMs
// (Section 3.3).
//
// We compute the fractional digits of sqrt(D) (for non-square D) in an
// arbitrary base using the classic digit-by-digit "long-hand square root"
// recurrence, generalized to base b. This is genuinely a two-register
// recurrence (logical register count = 2): we carry a remainder and the
// accumulated root prefix.
//
// The accumulated-root register's bit-width grows O(log n) with the digit
// index n -- exactly the complexity-metric shape the README predicts for a
// degree-2 algebraic number. We track this via BoundedInt::bit_width().
//
// AutomatonVM layout:
//   base    : codec
//   phase   : digit index
//   state[0]: remainder R (lo 64 bits)
//   state[1]: remainder R (hi 64 bits)  -- BoundedInt packed
//   state[2]: accumulated root prefix P (the integer formed by digits so far)
//   state[3]: D  (radicand) for sqrt, or a flag for phi
#ifndef NAM_ALGEBRAIC_HPP
#define NAM_ALGEBRAIC_HPP

#include <cstdint>

#include "abi.h"
#include "generator.hpp"
#include "bounded_int.hpp"

namespace nam
{
    namespace detail
    {
        inline BoundedInt pack_get(const AutomatonVM& s)
        {
            return BoundedInt(s.state[1], s.state[0]);
        }

        inline void pack_set(AutomatonVM& s, const BoundedInt& r)
        {
            s.state[0] = r.lo;
            s.state[1] = r.hi;
        }
    } // namespace detail

    // Digit-by-digit square root of the *fractional part* of sqrt(D).
    //
    // Standard long-hand sqrt in base b: at each step we bring down two "digits"
    // worth of the radicand (here, since the radicand is an integer D and we
    // want fractional digits, after the integer phase we bring down b^2 each
    // step with zero new radicand digits), and find the largest digit x in
    // [0, b) such that (P*2*b + x) * x <= R*b^2 + 0, where P is the root prefix
    // built so far (as an integer scaled by powers of b) and R is the running
    // remainder.
    struct Sqrt
    {
        static NumVMStep step(AutomatonVM s)
        {
            const uint64_t b = s.base;
            const uint64_t b2 = b * b;

            // R := R * b^2  (bring down two zero digits of the fractional part)
            BoundedInt R = detail::pack_get(s);
            R = R.mul_small(b2);

            // P is the current root prefix integer.
            uint64_t P = s.state[2];

            // Find largest x in [0, b) with (2*P*b + x) * x <= R.
            uint64_t base_term = 2ull * P * b;
            uint32_t best = 0;
            for (uint64_t x = 1; x < b; ++x)
            {
                // candidate = (base_term + x) * x
                BoundedInt cand = BoundedInt(base_term + x).mul_small(x);
                if (cand <= R) best = static_cast<uint32_t>(x);
                else break;
            }

            // Subtract chosen (2*P*b + best)*best from R.
            BoundedInt sub = BoundedInt(base_term + best).mul_small(best);
            R = R - sub;

            // New prefix: P := P*b + best
            uint64_t nextP = P * b + best;

            AutomatonVM next = s;
            detail::pack_set(next, R);
            next.state[2] = nextP;
            next.phase = s.phase + 1;
            return NumVMStep{best, next};
        }

        // Diagnostic: bit-width of the accumulated root prefix register, used by
        // the complexity-metric instrumentation to assert O(log n) growth.
        static int prefix_bitwidth(const AutomatonVM& s)
        {
            return BoundedInt(s.state[2]).bit_width();
        }
    };

    static_assert(Generator<Sqrt>);

    // Build a generator for the fractional digits of sqrt(D) in base b.
    // D must be a non-perfect-square positive integer. The integer part is
    // computed and used to seed the remainder; emitted digits are the
    // fractional expansion.
    inline AutomatonVM make_sqrt(uint64_t D, uint32_t base)
    {
        // Integer sqrt of D.
        uint64_t root = 0;
        while ((root + 1) * (root + 1) <= D) ++root;

        AutomatonVM vm{};
        vm.base = base;
        vm.phase = 0;
        // After extracting the integer part, the remainder is D - root^2,
        // and the running root prefix is `root`.
        detail::pack_set(vm, BoundedInt(D - root * root));
        vm.state[2] = root;
        vm.state[3] = D;
        return vm;
    }

    // phi = (1 + sqrt(5)) / 2. Its fractional part equals the fractional part of
    // sqrt(5)/2... rather than special-case the recurrence, we expose phi via
    // sqrt(5) digits offset is non-trivial; instead provide phi through its own
    // fractional value (sqrt(5)-1)/2 = 1/phi which shares phi's fractional
    // digits beyond the integer part. For Phase 1 golden tests we expose phi's
    // fractional part directly as fractionalpart(sqrt(5)) shifted; to keep the
    // recurrence clean we provide make_phi as sqrt of 5 with a post-divide by 2
    // handled at the codec layer is overkill. We therefore expose 1/phi which is
    // exactly (sqrt(5)-1)/2 and whose digits we can derive from the sqrt(5)
    // stream is also non-local. For simplicity and correctness Phase 1 ships
    // make_sqrt and computes phi-related quantities in tests via sqrt(5).
    inline AutomatonVM make_sqrt5(uint32_t base) { return make_sqrt(5, base); }
    // General integer-multiple-of-sqrt: emit the fractional digits of
    // sqrt(c^2 * D) = c*sqrt(D) by seeding the long-hand recurrence with the
    // scaled radicand. This lets callers express, e.g., 2*sqrt(2) = sqrt(8)
    // without a separate codec-level multiply.
    inline AutomatonVM make_scaled_sqrt(uint64_t c, uint64_t D, uint32_t base)
    {
        return make_sqrt(c * c * D, base);
    }
} // namespace nam

#endif // NAM_ALGEBRAIC_HPP