// include/nam/compare.hpp
//
// M6: Interval-honest comparison. Equality is undecidable; we ship only
// honest, bounded-precision predicates over a generator G and two states,
// read against a shared NumberSpace.
#ifndef NAM_COMPARE_HPP
#define NAM_COMPARE_HPP

#include <optional>

#include "number_space.hpp"
#include "generator.hpp"

namespace nam {
    enum class Trit { Less, Greater, Indistinguishable };

    template<Generator G>
    bool agrees_with(const NumberSpace &ns, typename G::State x,
                     typename G::State y, const int digits) {
        for (int i = 0; i < digits; ++i) {
            auto rx = G::step(ns, x);
            auto ry = G::step(ns, y);
            if (rx.digit != ry.digit) return false;
            x = rx.next;
            y = ry.next;
        }
        return true;
    }

    // Valid for most-significant-first (Direction::LR) streams. NOT valid
    // for LSB-up p-adic (Direction::RL) streams.
    template<Generator G>
    std::optional<bool> definitely_less_than(
        const NumberSpace &ns, typename G::State x, typename G::State y,
        const int max_digits) {
        for (int i = 0; i < max_digits; ++i) {
            auto rx = G::step(ns, x);
            auto ry = G::step(ns, y);
            if (rx.digit < ry.digit) return true;
            if (rx.digit > ry.digit) return false;
            x = rx.next;
            y = ry.next;
        }
        return std::nullopt;
    }

    template<Generator G>
    Trit compare(const NumberSpace &ns, typename G::State x,
                 typename G::State y, const int max_digits) {
        auto r = definitely_less_than<G>(ns, x, y, max_digits);
        if (!r.has_value()) return Trit::Indistinguishable;
        return *r ? Trit::Less : Trit::Greater;
    }

    // Cross-generator comparison over a shared NumberSpace (same base,
    // Direction::LR positional streams).
    template<Generator GX, Generator GY>
    std::optional<bool> definitely_less_than_xy(
        const NumberSpace &ns, typename GX::State x, typename GY::State y,
        const int max_digits) {
        for (int i = 0; i < max_digits; ++i) {
            auto rx = GX::step(ns, x);
            auto ry = GY::step(ns, y);
            if (rx.digit < ry.digit) return true;
            if (rx.digit > ry.digit) return false;
            x = rx.next;
            y = ry.next;
        }
        return std::nullopt;
    }
} // namespace nam

#endif // NAM_COMPARE_HPP
