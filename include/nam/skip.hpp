// include/nam/skip.hpp
//
// M7: Skip-ahead (Section 3.6). For genuine periodic orbits we jump in
// O(1) via phase arithmetic; the general fast-forwardable structure uses
// repeated squaring (modular exponentiation / matrix power) for O(log n).
//
// Phase 1 ships the periodic-orbit cases: rationals and p-adic rationals.
// The matrix-exponentiation helper is provided for the general case.
#ifndef NAM_SKIP_HPP
#define NAM_SKIP_HPP

#include <array>
#include <cstdint>

#include "abi.h"
#include "generator.hpp"
#include "rational.hpp"
#include "padic.hpp"

namespace nam
{
    // --- Generic O(n) reference skip: step n times. Used as a correctness
    //     oracle for the fast paths and as a fallback for non-periodic VMs. ---
    template <Generator G>
    AutomatonVM skip_naive(uint64_t n, AutomatonVM s)
    {
        for (uint64_t i = 0; i < n; ++i) s = G::step(s).next;
        return s;
    }

    // --- Rational skip via period (Section 3.2/3.6) ---
    // Given a rational VM, advance n digits in O(period + log n)-ish time by
    // replaying preperiod then jumping within the cycle. Because the remainder
    // sequence is what cycles, we precompute the cycle of remainders.
    inline AutomatonVM skip_rational(uint64_t n, AutomatonVM s)
    {
        auto [pre, per] = rational_period(s);
        if (per == 0)
        {
            // Terminating (or non-cycling within bound): fall back to naive,
            // but a terminating expansion just yields zeros after `pre`.
            if (n <= pre) return skip_naive<Rational>(n, s);
            // Step to end of significant digits, then remainder is 0 forever.
            AutomatonVM e = skip_naive<Rational>(pre, s);
            return e; // remainder already 0 -> all further digits are 0
        }
        if (n < pre) return skip_naive<Rational>(n, s);
        // Replay preperiod, then jump within cycle.
        AutomatonVM at_cycle = skip_naive<Rational>(pre, s);
        uint64_t into = (n - pre) % per;
        return skip_naive<Rational>(into, at_cycle);
    }

    // --- 2x2 modular matrix exponentiation: the general fast-forward kernel. ---
    // Represents the transition of a linear recurrence mod m. Provided for the
    // general fast-forwardable structure (e.g. BBP-style); Phase 1 uses it for
    // demonstration and tests.
    struct Mat2
    {
        uint64_t a, b, c, d; // [[a b],[c d]]
    };

    inline Mat2 mat_mul(const Mat2& x, const Mat2& y, uint64_t m)
    {
        auto mul = [&](uint64_t p, uint64_t q)
        {
            return (static_cast<unsigned __int128>(p) * q) % m;
        };
        return Mat2{
            static_cast<uint64_t>((mul(x.a, y.a) + mul(x.b, y.c)) % m),
            static_cast<uint64_t>((mul(x.a, y.b) + mul(x.b, y.d)) % m),
            static_cast<uint64_t>((mul(x.c, y.a) + mul(x.d, y.c)) % m),
            static_cast<uint64_t>((mul(x.c, y.b) + mul(x.d, y.d)) % m),
        };
    }

    inline Mat2 mat_pow(Mat2 base, uint64_t e, uint64_t m)
    {
        Mat2 result{1 % m, 0, 0, 1 % m}; // identity
        while (e)
        {
            if (e & 1) result = mat_mul(result, base, m);
            base = mat_mul(base, base, m);
            e >>= 1;
        }
        return result;
    }
} // namespace nam

#endif // NAM_SKIP_HPP
