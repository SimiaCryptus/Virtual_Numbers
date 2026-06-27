// include/nam/padic.hpp
//
// M5: p-adic numbers. Digit commitment is LOCAL. Digits are indexed from
// the least-significant upward -- i.e. these machines live on a NumberSpace
// with direction == Direction::RL. The prime p is ns.base.
//
// The step recurrence for x = a/b in Z_p (b invertible mod p):
//   digit d = (a * b^{-1}) mod p
//   a' = (a - d*b) / p          (exact integer division -- locality)
#ifndef NAM_PADIC_HPP
#define NAM_PADIC_HPP

#include <vector>
#include <utility>

#include "number_space.hpp"
#include "generator.hpp"

namespace nam {
    namespace detail {
        inline int64_t mod_inverse(const int64_t b, const int64_t p) {
            int64_t t = 0, newt = 1;
            int64_t r = p, newr = ((b % p) + p) % p;
            while (newr != 0) {
                const int64_t q = r / newr;
                int64_t tmp = t - q * newt;
                t = newt;
                newt = tmp;
                tmp = r - q * newr;
                r = newr;
                newr = tmp;
            }
            if (r > 1) return 0; // not invertible
            if (t < 0) t += p;
            return t;
        }

        inline int64_t floordiv(const int64_t a, const int64_t b) {
            int64_t q = a / b;
            if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
            return q;
        }
    } // namespace detail

    struct PAdic {
        struct State {
            int64_t a = 0; // current numerator (can be negative)
            int64_t b = 1; // denominator (invertible mod p)
        };

        static Step<State> step(const NumberSpace &ns, const State s) {
            const int64_t p = static_cast<int64_t>(ns.base);
            const int64_t binv = detail::mod_inverse(s.b, p);
            int64_t d = ((s.a % p) * binv) % p;
            d = ((d % p) + p) % p;
            const int64_t numerator = s.a - d * s.b;
            State next = s;
            next.a = numerator / p; // exact
            return Step<State>{static_cast<uint32_t>(d), next};
        }
    };

    static_assert(Generator<PAdic>);

    // Build the p-adic state for a/b in Z_p. Requires gcd(b,p) == 1. The
    // NumberSpace should carry base = p and direction = Direction::RL.
    inline PAdic::State make_padic_state(const int64_t a, const int64_t b) {
        return PAdic::State{a, b};
    }

    inline NumberSpace padic_space(const uint32_t p) {
        return NumberSpace{p, Direction::RL, 0};
    }

    inline int64_t p_valuation(int64_t n, const int64_t p) {
        if (n == 0) return -1;
        int64_t v = 0;
        while (n % p == 0) {
            n /= p;
            ++v;
        }
        return v;
    }

    inline std::pair<uint64_t, uint64_t> padic_period(
        const NumberSpace &ns, PAdic::State s) {
        std::vector<std::pair<int64_t, uint64_t> > seen;
        uint64_t idx = 0;
        while (true) {
            for (auto &[aa, i]: seen) {
                if (aa == s.a) return {i, idx - i};
            }
            seen.emplace_back(s.a, idx);
            s = PAdic::step(ns, s).next;
            ++idx;
            if (idx > 1'000'000) return {idx, 0};
        }
    }
} // namespace nam

#endif // NAM_PADIC_HPP