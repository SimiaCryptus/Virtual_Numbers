// include/nam/metric.hpp
//
// M6: The p-adic metric as a product automaton.
//
// Run two p-adic generators in lockstep on a shared NumberSpace (direction
// RL); the first differing digit index IS the valuation of the difference.
#ifndef NAM_METRIC_HPP
#define NAM_METRIC_HPP

#include <cmath>
#include <optional>

#include "number_space.hpp"
#include "generator.hpp"

namespace nam {
    template<Generator G>
    std::optional<int> padic_valuation_of_difference(
        const NumberSpace &ns, typename G::State x, typename G::State y,
        const int max_digits) {
        for (int i = 0; i < max_digits; ++i) {
            auto rx = G::step(ns, x);
            auto ry = G::step(ns, y);
            if (rx.digit != ry.digit) return i;
            x = rx.next;
            y = ry.next;
        }
        return std::nullopt;
    }

    template<Generator G>
    std::optional<double> padic_distance(
        const NumberSpace &ns, typename G::State x, typename G::State y,
        const int max_digits) {
        auto v = padic_valuation_of_difference<G>(ns, x, y, max_digits);
        if (!v.has_value()) return std::nullopt;
        const double p = ns.base;
        return std::pow(p, -static_cast<double>(*v));
    }
} // namespace nam

#endif // NAM_METRIC_HPP
