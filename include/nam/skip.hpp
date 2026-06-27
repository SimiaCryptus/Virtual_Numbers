// include/nam/skip.hpp
//
// M7: Skip-ahead. Periodic orbits jump in O(1) via phase arithmetic; the
// general fast-forwardable structure uses repeated squaring for O(log n).
#ifndef NAM_SKIP_HPP
#define NAM_SKIP_HPP

#include "number_space.hpp"
#include "generator.hpp"
#include "rational.hpp"

namespace nam {
    // Generic O(n) reference skip: correctness oracle / fallback.
    template<Generator G>
    G::State skip_naive(const NumberSpace &ns, const uint64_t n,
                        typename G::State s) {
        for (uint64_t i = 0; i < n; ++i) s = G::step(ns, s).next;
        return s;
    }

    // Rational skip via period detection.
    inline Rational::State skip_rational(const NumberSpace &ns, const uint64_t n,
                                         const Rational::State &s) {
        auto [pre, per] = rational_period(ns, s);
        if (per == 0) {
            if (n <= pre) return skip_naive<Rational>(ns, n, s);
            return skip_naive<Rational>(ns, pre, s); // remainder 0 forever
        }
        if (n < pre) return skip_naive<Rational>(ns, n, s);
        const Rational::State at_cycle = skip_naive<Rational>(ns, pre, s);
        const uint64_t into = (n - pre) % per;
        return skip_naive<Rational>(ns, into, at_cycle);
    }

    // --- 2x2 modular matrix exponentiation: general fast-forward kernel. ---
    struct Mat2 {
        uint64_t a, b, c, d;
    };

    inline Mat2 mat_mul(const Mat2 &x, const Mat2 &y, const uint64_t m) {
        auto mul = [&](const uint64_t p, const uint64_t q) {
            return (static_cast<unsigned __int128>(p) * q) % m;
        };
        return Mat2{
            static_cast<uint64_t>((mul(x.a, y.a) + mul(x.b, y.c)) % m),
            static_cast<uint64_t>((mul(x.a, y.b) + mul(x.b, y.d)) % m),
            static_cast<uint64_t>((mul(x.c, y.a) + mul(x.d, y.c)) % m),
            static_cast<uint64_t>((mul(x.c, y.b) + mul(x.d, y.d)) % m),
        };
    }

    inline Mat2 mat_pow(Mat2 base, uint64_t e, const uint64_t m) {
        Mat2 result{1 % m, 0, 0, 1 % m};
        while (e) {
            if (e & 1) result = mat_mul(result, base, m);
            base = mat_mul(base, base, m);
            e >>= 1;
        }
        return result;
    }
} // namespace nam

#endif // NAM_SKIP_HPP
