// include/nam/generator.hpp
//
// C++20 Generator concept over the NumVMFn ABI, plus the ABI guardrails and
// the O(1) struct-copy fork. (Section 2.2, 2.3, 3.1.)
#ifndef NAM_GENERATOR_HPP
#define NAM_GENERATOR_HPP

#include <concepts>
#include <type_traits>

#include "abi.h"

namespace nam
{
    // ---- ABI guardrails (Section 2.2): write these as compile-time proofs. ----
    static_assert(sizeof(AutomatonVM) == 40,
                  "AutomatonVM must be 4 + 4 + 4*8 == 40 bytes");
    static_assert(std::is_trivially_copyable_v<AutomatonVM>,
                  "fork must be a literal struct copy");
    static_assert(std::is_standard_layout_v<AutomatonVM>,
                  "AutomatonVM must be C-ABI safe");
    static_assert(sizeof(NumVMStep) == 48 || sizeof(NumVMStep) <= 56,
                  "NumVMStep is digit + AutomatonVM (padding-permitting)");

    // ---- The Generator concept (Section 3.1). ----
    // Anything that exposes a static `step(AutomatonVM) -> NumVMStep` satisfies
    // it. Keeping step static + inline lets Clang inline across the boundary.
    template <typename G>
    concept Generator = requires(AutomatonVM s)
    {
        { G::step(s) } -> std::same_as<NumVMStep>;
    };

    // ---- Fork: the whole point of Phase 1 (Section 2.3). ----
    // Pure value copy. O(1). No hidden state. Fully and without qualification
    // true for the automaton tier.
    [[nodiscard]] static inline constexpr AutomatonVM num_vm_fork(AutomatonVM s)
    {
        return s;
    }

    // Convenience: emit the first `n` digits of a generator into `out`.
    template <Generator G, typename It>
    constexpr void take(AutomatonVM s, int n, It out)
    {
        for (int i = 0; i < n; ++i)
        {
            NumVMStep r = G::step(s);
            *out++ = r.digit;
            s = r.next;
        }
    }

    // Emit digits while a predicate holds, up to a hard cap `max_n` to keep
    // the loop total over potentially-infinite streams. Returns the number of
    // digits emitted. `pred(digit, index)` decides whether to continue.
    template <Generator G, typename It, typename Pred>
    constexpr int take_while(AutomatonVM s, int max_n, It out, Pred pred)
    {
        int i = 0;
        for (; i < max_n; ++i)
        {
            NumVMStep r = G::step(s);
            if (!pred(r.digit, i)) break;
            *out++ = r.digit;
            s = r.next;
        }
        return i;
    }

    // Advance the generator `n` steps without emitting, returning the final
    // state. O(n); periodic generators should prefer skip.hpp fast paths.
    template <Generator G>
    constexpr AutomatonVM drop(AutomatonVM s, int n)
    {
        for (int i = 0; i < n; ++i) s = G::step(s).next;
        return s;
    }
} // namespace nam

#endif // NAM_GENERATOR_HPP