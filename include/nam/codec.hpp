// include/nam/codec.hpp
//
// M4: Codec layer -- base as projection. With the NumberSpace model the
// base is no longer baked into a machine's state, so reprojecting a
// rational is just changing the space's base: the (p,q) state is
// base-agnostic. We keep a generic streaming reprojector for arbitrary
// generators whose state IS base-coupled (e.g. Sqrt's prefix register).
#ifndef NAM_CODEC_HPP
#define NAM_CODEC_HPP

#include <vector>

#include "number_space.hpp"
#include "generator.hpp"
#include "rational.hpp"

namespace nam {
    // Rationals keep identity under base change: the (p,q) state is
    // unchanged, only the NumberSpace's base differs. Provided for symmetry
    // / clarity -- callers can equally just call ns.in_base(new_base).
    inline NumberSpace rational_in_base(const NumberSpace &ns,
                                        const uint32_t new_base) {
        return ns.in_base(new_base);
    }

    // Generic streaming reprojector. Reads `src_digits` of a value in [0,1)
    // produced by G under `src` space, forms the exact rational num/den,
    // then emits `out_digits` of that rational in `target_base`.
    template<Generator G>
    std::vector<uint32_t> reproject_digits(
        const NumberSpace &src, typename G::State seed,
        const uint32_t target_base, const int src_digits, const int out_digits) {
        uint64_t num = 0;
        uint64_t den = 1;
        typename G::State s = seed;
        for (int i = 0; i < src_digits; ++i) {
            auto r = G::step(src, s);
            num = num * src.base + r.digit;
            den = den * src.base;
            s = r.next;
        }

        std::vector<uint32_t> out;
        out.reserve(out_digits);
        const NumberSpace target = src.in_base(target_base);
        Rational::State rv = make_rational_state(num, den);
        for (int i = 0; i < out_digits; ++i) {
            auto r = Rational::step(target, rv);
            out.push_back(r.digit);
            rv = r.next;
        }
        return out;
    }
} // namespace nam

#endif // NAM_CODEC_HPP
