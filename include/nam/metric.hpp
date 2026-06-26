// include/nam/metric.hpp
//
// M6: The p-adic metric as a product automaton (Section 3.7).
//
// The p-adic distance between x and y is p^{-v_p(x-y)}, where v_p(x-y) is
// the index of the first digit (LSB-up) where their expansions differ.
//
// Because both inputs are p-adic generators, we run them in lockstep -- a
// product automaton over the two state spaces -- and the first differing
// digit index IS the valuation of the difference. This is exact and, when
// both inputs are periodic, itself a finite periodic machine.
#ifndef NAM_METRIC_HPP
#define NAM_METRIC_HPP

#include <cmath>
#include <optional>

#include "nam/abi.h"
#include "nam/generator.hpp"

namespace nam
{
    // Returns the valuation v_p(x - y): the LSB-up index of the first differing
    // digit, scanning up to max_digits. nullopt means "agree on the whole
    // prefix" (distance is < p^{-max_digits}; we honestly report pending).
    template <Generator G>
    std::optional<int> padic_valuation_of_difference(AutomatonVM x, AutomatonVM y,
                                                     int max_digits)
    {
        for (int i = 0; i < max_digits; ++i)
        {
            NumVMStep rx = G::step(x);
            NumVMStep ry = G::step(y);
            if (rx.digit != ry.digit) return i;
            x = rx.next;
            y = ry.next;
        }
        return std::nullopt;
    }

    // p-adic distance p^{-v}. Returns nullopt if the prefix fully agrees within
    // max_digits (distance smaller than resolvable bound).
    template <Generator G>
    std::optional<double> padic_distance(AutomatonVM x, AutomatonVM y,
                                         int max_digits)
    {
        auto v = padic_valuation_of_difference<G>(x, y, max_digits);
        if (!v.has_value()) return std::nullopt;
        double p = static_cast<double>(x.base);
        return std::pow(p, -static_cast<double>(*v));
    }
} // namespace nam

#endif // NAM_METRIC_HPP