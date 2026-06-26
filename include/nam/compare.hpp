// include/nam/compare.hpp
//
// M6: Interval-honest comparison (Section 3.7). Equality is undecidable;
// we ship only honest, bounded-precision predicates. These operate on a
// generator type G and two automaton states.
#ifndef NAM_COMPARE_HPP
#define NAM_COMPARE_HPP

#include <optional>

#include "nam/abi.h"
#include "nam/generator.hpp"

namespace nam
{
    enum class Trit { Less, Greater, Indistinguishable };

    // agrees_with: exact for a finite prefix of `digits` digits.
    template <Generator G>
    bool agrees_with(AutomatonVM x, AutomatonVM y, int digits)
    {
        for (int i = 0; i < digits; ++i)
        {
            NumVMStep rx = G::step(x);
            NumVMStep ry = G::step(y);
            if (rx.digit != ry.digit) return false;
            x = rx.next;
            y = ry.next;
        }
        return true;
    }

    // definitely_less_than: scans up to max_digits. Returns:
    //   true  -> x is provably < y (found a digit position where x<y first)
    //   false (with engaged optional == false) -> provably >= (x>y found first)
    //   std::nullopt -> pending / indistinguishable within max_digits
    //
    // NOTE: this is for *most-significant-first* digit streams (reals/rationals
    // in a positional base). It is NOT valid for LSB-up p-adic streams.
    template <Generator G>
    std::optional<bool> definitely_less_than(AutomatonVM x, AutomatonVM y,
                                             int max_digits)
    {
        for (int i = 0; i < max_digits; ++i)
        {
            NumVMStep rx = G::step(x);
            NumVMStep ry = G::step(y);
            if (rx.digit < ry.digit) return true;
            if (rx.digit > ry.digit) return false;
            x = rx.next;
            y = ry.next;
        }
        return std::nullopt; // pending -- never a false definite answer
    }

    template <Generator G>
    Trit compare(AutomatonVM x, AutomatonVM y, int max_digits)
    {
        auto r = definitely_less_than<G>(x, y, max_digits);
        if (!r.has_value()) return Trit::Indistinguishable;
        return *r ? Trit::Less : Trit::Greater;
    }
} // namespace nam

#endif // NAM_COMPARE_HPP
