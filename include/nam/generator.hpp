// include/nam/generator.hpp
//
// The Generator concept over the NumberSpace model.
//
// A generator G owns a value type G::State and exposes
//
//     static Step<State> step(const NumberSpace&, State);
//
// The NumberSpace is the shared coordinate frame (base/direction/scale);
// the State is the generator's own register file -- whatever shape it
// needs, no longer a frozen 40-byte blob. Fork is, for the automaton tier,
// still a literal value copy of State (O(1)); the series tier documents its
// own deep-copy cost.
#ifndef NAM_GENERATOR_HPP
#define NAM_GENERATOR_HPP

#include <concepts>

#include "number_space.hpp"

namespace nam {
    // ---- The Generator concept. ----
    template<typename G>
    concept Generator = requires(NumberSpace ns, typename G::State s)
    {
        typename G::State;
        { G::step(ns, s) } -> std::same_as<Step<typename G::State> >;
    };

    // ---- Fork: pure value copy of the generator state. O(1) for the
    //      automaton tier (trivially-copyable State). ----
    template<Generator G>
    [[nodiscard]] constexpr G::State
    fork(const typename G::State &s) {
        return s;
    }

    // Convenience: emit the first `n` digits of a generator into `out`.
    template<Generator G, typename It>
    constexpr void take(const NumberSpace &ns, typename G::State s,
                        const int n, It out) {
        for (int i = 0; i < n; ++i) {
            auto r = G::step(ns, s);
            *out++ = r.digit;
            s = r.next;
        }
    }

    // Emit digits while a predicate holds, up to a hard cap `max_n`.
    // Returns the number of digits emitted. `pred(digit, index)`.
    template<Generator G, typename It, typename Pred>
    constexpr int take_while(const NumberSpace &ns, typename G::State s,
                             const int max_n, It out, Pred pred) {
        int i = 0;
        for (; i < max_n; ++i) {
            auto r = G::step(ns, s);
            if (!pred(r.digit, i)) break;
            *out++ = r.digit;
            s = r.next;
        }
        return i;
    }

    // Advance `n` steps without emitting, returning the final state.
    template<Generator G>
    constexpr G::State drop(const NumberSpace &ns,
                            typename G::State s, const int n) {
        for (int i = 0; i < n; ++i) s = G::step(ns, s).next;
        return s;
    }
} // namespace nam

#endif // NAM_GENERATOR_HPP
