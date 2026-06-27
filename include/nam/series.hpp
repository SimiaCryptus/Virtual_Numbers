// include/nam/series.hpp
//
// Phase 2: The SERIES tier (THEORY.md "The Forkable Nano-VM: A Two-Tier
// ABI" and "The Odd Primitives").
//
// The series tier represents classical transcendentals (e, pi, log 2,
// zeta(3), ...) as convergent series with an attached tail-bound oracle.
// Unlike the automaton tier, its state GROWS with computation depth, so
// fork is an explicit deep copy of the BigInt accumulators -- O(log n) in
// the depth -- and is documented and instrumented as such.
//
// The library tracks the value as an exact rational num/den (BigInt) plus
// a tail-bound numerator: |true_value - num/den| <= err_num/den. Digits
// are committed via interval refinement: a digit is emitted only when the
// interval [num/den, (num+err_num)/den] is narrow enough that the leading
// digit is unambiguous (honest, never a false commit).
//
// No global mutable state; the optional LRU memo (memo.hpp) is an explicit
// wrapper the user opts into.
#ifndef NAM_SERIES_HPP
#define NAM_SERIES_HPP

#include <functional>
#include <memory>

#include "big_int.hpp"

namespace nam {
    // ---- Series specification (immutable, safe to alias / share) ----
    //
    // A SeriesSpec describes how to advance a partial-sum accumulator and how
    // to bound the remaining tail. Following the ABI sketch in THEORY.md, the
    // spec is immutable and only the accumulators are mutable per-VM state.
    //
    // We accumulate the partial sum S_n as an exact rational num/den. The
    // step advances index -> index+1, updating (num, den). The tail oracle
    // returns err such that |value - num/den| <= err/den.
    struct SeriesSpec {
        // Advance: given index n and current (num, den), produce next
        // (num, den) representing S_{n+1}. Implementations keep den as a
        // running common denominator.
        std::function<void(uint64_t /*index*/, BigInt & /*num*/,
                           BigInt & /*den*/)> advance;

        // Tail bound: given index n and current den, return err (a BigInt)
        // such that the true value differs from num/den by at most err/den.
        // Must be monotone non-increasing as index grows (a compiled
        // convergence proof, THEORY.md "Tail Bound Oracle").
        std::function<BigInt(uint64_t /*index*/, const BigInt & /*den*/)>
        tail_bound;

        // Human-readable name for diagnostics / golden tests.
        const char *name = "series";
    };

    // ---- Series VM: mutable accumulators, explicit deep-copy fork ----
    //
    // This mirrors the SeriesVM ABI struct in THEORY.md: base + index +
    // immutable spec pointer + mutable accumulators. We use a shared_ptr for
    // the immutable spec (safe to alias) and value BigInts for the mutable
    // accumulators (deep-copied on fork).
    struct SeriesVM {
        uint32_t base = 10;
        uint64_t index = 0; // number of terms accumulated
        std::shared_ptr<const SeriesSpec> spec;
        BigInt num; // partial-sum numerator (mutable)
        BigInt den; // common denominator (mutable)

        // Fork: explicit DEEP COPY of accumulators. The spec is shared
        // (immutable). This is O(size of accumulators) == O(log n) in depth,
        // exactly as documented in THEORY.md. NOT copy-on-write.
        SeriesVM fork() const {
            SeriesVM c;
            c.base = base;
            c.index = index;
            c.spec = spec; // alias immutable spec -- safe
            c.num = num; // deep copy
            c.den = den; // deep copy
            return c;
        }

        // Advance one term.
        void step_term() {
            spec->advance(index, num, den);
            ++index;
        }

        // Current tail bound err/den.
        BigInt tail() const { return spec->tail_bound(index, den); }

        // Total live accumulator bit-width: the complexity metric for the
        // series tier (memory IS the metric, THEORY.md).
        int accumulator_bitwidth() const {
            return num.bit_width() + den.bit_width();
        }

        // Advance until the certified interval width err/den is narrow enough
        // that at least `target_digits` base-`base` digits are pinned, or the
        // step budget is exhausted. Returns the number of terms stepped. This
        // is a convenience the refiner can call to batch-converge instead of
        // stepping one term per digit attempt.
        uint64_t converge_to_digits(const int target_digits, const int max_steps = 100000) {
            uint64_t stepped = 0;
            // Required: err/den < base^{-target_digits}, i.e.
            // err * base^target_digits < den.
            const BigInt threshold = big_pow(BigInt(base),
                                             static_cast<uint64_t>(target_digits));
            for (int i = 0; i < max_steps; ++i) {
                if (index > 0) {
                    BigInt err = tail();
                    if (err * threshold < den) break;
                }
                step_term();
                ++stepped;
            }
            return stepped;
        }
    };

    // ---- Construct a SeriesVM from a spec ----
    inline SeriesVM make_series(std::shared_ptr<const SeriesSpec> spec,
                                const uint32_t base) {
        SeriesVM vm;
        vm.base = base;
        vm.index = 0;
        vm.spec = std::move(spec);
        vm.num = BigInt(0);
        vm.den = BigInt(1);
        return vm;
    }
} // namespace nam

#endif // NAM_SERIES_HPP
