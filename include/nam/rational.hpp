// include/nam/rational.hpp
//
// M2: Rationals as constant-state periodic VMs (Section 3.2).
//
// A rational p/q in base b is a constant-state machine: state[0] holds the
// running remainder, base holds the codec. The struct does NOT grow as
// digits are emitted (constant memory => logical register count 1).
//
// Digit step is long-division-by-multiplication:
//   r = r * b; digit = r / q; r = r % q;
#ifndef NAM_RATIONAL_HPP
#define NAM_RATIONAL_HPP

#include <cstdint>
#include <vector>
#include <utility>

#include "abi.h"
#include "generator.hpp"

namespace nam {
    // Layout of AutomatonVM for a Rational:
    //   base    : codec (digit alphabet size)
    //   phase   : digit index (informational; not required for stepping)
    //   state[0]: running remainder r, with 0 <= r < q
    //   state[1]: denominator q
    //   state[2]: numerator p (only meaningful at construction; preserved)
    struct Rational {
        static constexpr NumVMStep step(AutomatonVM s) {
            uint64_t q = s.state[1];
            uint64_t r = s.state[0];
            if (q == 0) {
                // Terminating / degenerate denominator: emit zeros.
                AutomatonVM next = s;
                next.phase = s.phase + 1;
                return NumVMStep{0u, next};
            }
            // r * base then split.
            // r < q <= ~2^32 for sane inputs; base small => fits in 64 bits.
            uint64_t scaled = r * static_cast<uint64_t>(s.base);
            uint32_t digit = static_cast<uint32_t>(scaled / q);
            uint64_t next_r = scaled % q;

            AutomatonVM next = s;
            next.state[0] = next_r;
            next.phase = s.phase + 1;
            return NumVMStep{digit, next};
        }
    };

    static_assert(Generator<Rational>);

    // Construct a fractional-part rational generator for (p mod q) / q in base b.
    // The integer part is dropped; this emits the digits to the right of the
    // radix point. (Phase 1 focuses on fractional digit streams.)
    inline AutomatonVM make_rational(uint64_t p, uint64_t q, uint32_t base) {
        AutomatonVM vm{};
        vm.base = base;
        vm.phase = 0;
        vm.state[0] = (q != 0) ? (p % q) : 0; // initial remainder
        vm.state[1] = q;
        vm.state[2] = p;
        return vm;
    }

    // Period detection: walk the remainder sequence until a remainder repeats.
    // Returns {preperiod_length, period_length}. Period 0 means terminating.
    inline std::pair<uint64_t, uint64_t> rational_period(AutomatonVM vm) {
        std::vector<std::pair<uint64_t, uint64_t> > seen; // (remainder, index)
        uint64_t idx = 0;
        while (true) {
            uint64_t r = vm.state[0];
            if (r == 0) {
                return {idx, 0}; // terminating expansion
            }
            for (auto &[rr, i]: seen) {
                if (rr == r) {
                    return {i, idx - i}; // preperiod, period
                }
            }
            seen.emplace_back(r, idx);
            vm = Rational::step(vm).next;
            ++idx;
            if (idx > 1'000'000) return {idx, 0}; // safety bound
        }
    }
} // namespace nam

#endif // NAM_RATIONAL_HPP
