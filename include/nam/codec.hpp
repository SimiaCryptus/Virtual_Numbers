// include/nam/codec.hpp
//
// M4: Codec layer -- base as projection, not baked into the number
// (Section 3.5).
//
// The base lives in the `base` field. Changing base changes the projection,
// not the number. We implement base conversion as a re-decoding wrapper:
// we read enough source-base digits to pin the value to a sufficiently small
// interval, then emit target-base digits.
//
// Phase 1 keeps this concrete and interval-honest: for sources whose value
// is a fraction in [0,1) (Rational fractional streams, Sqrt fractional
// streams scaled into [0,1)), reproject by repeated multiply-by-target-base
// using a rational tracking of the consumed prefix. To stay exact for the
// round-trip property tests we reproject *Rational* sources analytically
// (re-seed make_rational with the new base) and provide a generic streaming
// reprojector for arbitrary generators.
#ifndef NAM_CODEC_HPP
#define NAM_CODEC_HPP

#include <cstdint>
#include <vector>

#include "abi.h"
#include "generator.hpp"
#include "rational.hpp"

namespace nam {
    // Analytic reprojection for rationals: a rational keeps its identity under a
    // base change; we simply rebuild the VM with the new codec. This is the
    // cleanest demonstration that "base is a codec".
    inline AutomatonVM rational_in_base(AutomatonVM rat, uint32_t new_base) {
        // state[2] = p, state[1] = q  (as seeded by make_rational)
        return make_rational(rat.state[2], rat.state[1], new_base);
    }

    // Generic streaming reprojector. Reads `src` (a generator producing digits
    // in src.base for a value in [0,1)) and emits digits in `target_base`.
    //
    // Implemented as a host-side helper (not an AutomatonVM step), because a
    // faithful streaming reprojection needs a growing rational accumulator,
    // which is a Phase-2 (series-tier) concern. For Phase 1 we expose it as a
    // bounded host helper used in tests and by the rational fast path above.
    template<Generator G>
    std::vector<uint32_t> reproject_digits(AutomatonVM src, uint32_t target_base,
                                           int src_digits, int out_digits) {
        // Read src_digits of the source value into an exact rational fraction
        // num/den where den = src.base^src_digits.
        // num/den is an exact lower bound on the value (it ignores the tail).
        uint64_t num = 0;
        uint64_t den = 1;
        AutomatonVM s = src;
        for (int i = 0; i < src_digits; ++i) {
            NumVMStep r = G::step(s);
            num = num * src.base + r.digit;
            den = den * src.base;
            s = r.next;
        }

        // Now emit out_digits of num/den in target_base via the Rational VM.
        std::vector<uint32_t> out;
        out.reserve(out_digits);
        AutomatonVM rv = make_rational(num, den, target_base);
        for (int i = 0; i < out_digits; ++i) {
            NumVMStep r = Rational::step(rv);
            out.push_back(r.digit);
            rv = r.next;
        }
        return out;
    }
} // namespace nam

#endif // NAM_CODEC_HPP
