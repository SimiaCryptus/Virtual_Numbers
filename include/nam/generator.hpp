// include/nam/generator.hpp
//
// C++20 Generator concept over the NumVMFn ABI, plus the ABI guardrails and
// the O(1) struct-copy fork. (Section 2.2, 2.3, 3.1.)
#ifndef NAM_GENERATOR_HPP
#define NAM_GENERATOR_HPP

#include <concepts>
#include <type_traits>

#include "nam/abi.h"

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
} // namespace nam

#endif // NAM_GENERATOR_HPP