// include/nam/rational.hpp
//
// M2: Rationals as constant-state periodic VMs.
//
// A rational p/q in base b is a constant-state machine: the state holds the
// running remainder (plus p,q for identity / reprojection). base, direction
// and scale now live in the NumberSpace passed to step(), NOT in the state.
//
// Digit step is long-division-by-multiplication:
//   r = r * b; digit = r / q; r = r % q;
#ifndef NAM_RATIONAL_HPP
#define NAM_RATIONAL_HPP

#include <vector>
#include <utility>

#include "number_space.hpp"
#include "generator.hpp"

namespace nam {
    struct Rational {
        // The genuine register file for a rational digit machine.
        struct State {
            uint64_t r = 0; // running remainder, 0 <= r < q
            uint64_t q = 0; // denominator
            uint64_t p = 0; // numerator (preserved for identity/reproject)
        };

        static constexpr Step<State> step(const NumberSpace &ns, const State &s) {
            if (s.q == 0) {
                // Terminating / degenerate denominator: emit zeros.
                return Step{0u, s};
            }
            const uint64_t scaled = s.r * static_cast<uint64_t>(ns.base);
            const auto digit = static_cast<uint32_t>(scaled / s.q);
            State next = s;
            next.r = scaled % s.q;
            return Step{digit, next};
        }
    };

    static_assert(Generator<Rational>);

    // Construct the (p mod q)/q fractional-part state. The NumberSpace
    // (base/direction/scale) is supplied separately by the caller.
    inline Rational::State make_rational_state(const uint64_t p, const uint64_t q) {
        Rational::State s;
        s.q = q;
        s.p = p;
        s.r = (q != 0) ? (p % q) : 0;
        return s;
    }

    // Period detection: walk the remainder sequence until a remainder
    // repeats. Returns {preperiod_length, period_length}. Period 0 means
    // terminating.
    inline std::pair<uint64_t, uint64_t> rational_period(
        const NumberSpace &ns, Rational::State s) {
        std::vector<std::pair<uint64_t, uint64_t> > seen; // (remainder, idx)
        uint64_t idx = 0;
        while (true) {
            uint64_t r = s.r;
            if (r == 0) {
                return {idx, 0}; // terminating expansion
            }
            for (auto &[rr, i]: seen) {
                if (rr == r) {
                    return {i, idx - i}; // preperiod, period
                }
            }
            seen.emplace_back(r, idx);
            s = Rational::step(ns, s).next;
            ++idx;
            if (idx > 1'000'000) return {idx, 0}; // safety bound
        }
    }
} // namespace nam

#endif // NAM_RATIONAL_HPP
