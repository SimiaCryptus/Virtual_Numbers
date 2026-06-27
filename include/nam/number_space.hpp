// include/nam/number_space.hpp
//
// The NumberSpace: the coordinate system every machine lives on.
//
// This REPLACES the old frozen C ABI (abi.h). With LLVM/JIT off the
// roadmap there is no reason to pin a 40-byte POD layout or to bake the
// Phase-1 automaton register file (base + phase + state[4]) into the
// fundamental structures. Instead, the *positional semantics* that used
// to be smeared across every struct now live in one small value type:
//
//   base      : the digit alphabet size (radix / codec selector).
//   direction : LR = most-significant-first (reals, rationals),
//               RL = least-significant-first (p-adics).
//   scale     : signed exponent of `base`. The value a machine emits is
//               interpreted as  (digit stream) * base^scale. scale = 0
//               means a fractional value in [0,1) (the Phase-1 default);
//               scale shifts the radix point without touching the digits.
//
// A machine (generator) carries its OWN state type; the NumberSpace is the
// shared coordinate frame the digits are read against. base is no longer a
// field smuggled inside every register file -- it is a property of the
// space, passed alongside the state.
#ifndef NAM_NUMBER_SPACE_HPP
#define NAM_NUMBER_SPACE_HPP

#include <cstdint>

namespace nam {
    enum class Direction : uint8_t {
        LR = 0, // most-significant digit first (positional reals/rationals)
        RL = 1, // least-significant digit first (p-adic)
    };

    struct NumberSpace {
        uint32_t base = 10;
        Direction direction = Direction::LR;
        int32_t scale = 0; // signed exponent of base; radix-point shift

        constexpr NumberSpace() = default;

        constexpr NumberSpace(const uint32_t b,
                              const Direction d = Direction::LR,
                              const int32_t s = 0)
            : base(b), direction(d), scale(s) {
        }

        // Reproject into a new base, preserving direction/scale. "base is a
        // codec" -- changing it changes the projection, not the number.
        [[nodiscard]] [[nodiscard]] [[nodiscard]] constexpr NumberSpace in_base(const uint32_t new_base) const {
            return NumberSpace{new_base, direction, scale};
        }

        // Shift the radix point by `d` digit posi[[nodiscard]] tions (scale +[[nodiscard]] = d).
        [[nodiscard]] constexpr NumberSpace shifted(const int32_t d) const {
            return NumberSpace{base, direction, scale + d};
        }

        friend constexpr bool operator==(const NumberSpace &a,
                                         const NumberSpace &b) {
            return a.base == b.base && a.direction == b.direction &&
                   a.scale == b.scale;
        }
    };

    // A single produced digit plus the successor state. The state type is a
    // generator-specific value (NOT a frozen register file). `Step<S>` is
    // what a generator's `step` returns.
    template<typename State>
    struct Step {
        uint32_t digit;
        State next;
    };
} // namespace nam

#endif // NAM_NUMBER_SPACE_HPP
