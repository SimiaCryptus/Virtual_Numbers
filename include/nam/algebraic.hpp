// include/nam/algebraic.hpp
//
// M3: Quadratic irrationals (sqrt(D), phi) as degree-2 recurrence VMs.
//
// Digit-by-digit "long-hand square root" in an arbitrary base. Genuinely a
// two-register recurrence: a remainder R (BoundedInt) and the accumulated
// root prefix P. base lives in the NumberSpace, not in the state.
#ifndef NAM_ALGEBRAIC_HPP
#define NAM_ALGEBRAIC_HPP

#include "number_space.hpp"
#include "generator.hpp"
#include "bounded_int.hpp"

namespace nam {
    struct Sqrt {
        struct State {
            BoundedInt R{}; // running remainder
            uint64_t P = 0; // accumulated root prefix
            uint64_t D = 0; // radicand (preserved for identity)
        };

        static Step<State> step(const NumberSpace &ns, const State &s) {
            const uint64_t b = ns.base;
            const uint64_t b2 = b * b;

            BoundedInt R = s.R.mul_small(b2);
            const uint64_t P = s.P;

            const uint64_t base_term = 2ull * P * b;
            uint32_t best = 0;
            for (uint64_t x = 1; x < b; ++x) {
                BoundedInt cand = BoundedInt(base_term + x).mul_small(x);
                if (cand <= R) best = static_cast<uint32_t>(x);
                else break;
            }

            const BoundedInt sub = BoundedInt(base_term + best).mul_small(best);
            R = R - sub;

            State next = s;
            next.R = R;
            next.P = P * b + best;
            return Step{best, next};
        }

        // Bit-width of the accumulated root prefix register (O(log n)).
        static int prefix_bitwidth(const State &s) {
            return BoundedInt(s.P).bit_width();
        }
    };

    static_assert(Generator<Sqrt>);

    // Build the sqrt(D) fractional-digit state for non-perfect-square D.
    inline Sqrt::State make_sqrt_state(const uint64_t D) {
        uint64_t root = 0;
        while ((root + 1) * (root + 1) <= D) ++root;
        Sqrt::State s;
        s.R = BoundedInt(D - root * root);
        s.P = root;
        s.D = D;
        return s;
    }

    inline Sqrt::State make_sqrt5_state() { return make_sqrt_state(5); }

    inline Sqrt::State make_scaled_sqrt_state(const uint64_t c, const uint64_t D) {
        return make_sqrt_state(c * c * D);
    }
} // namespace nam

#endif // NAM_ALGEBRAIC_HPP
