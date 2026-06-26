// include/nam/refine.hpp
//
// Phase 2: Interval refinement + online digit extraction for the series
// tier (THEORY.md "Interval Refinement Engine").
//
// We maintain a shrinking interval [L, U] containing the true value, both
// as rationals over a common denominator. A digit in `base` is emitted
// only when every value in [L, U] shares the same next digit -- i.e. the
// interval, once scaled into the current digit position, collapses to a
// single integer floor. This is interval-honest: exact-boundary values
// (e.g. exactly 0.5 in base 10) may legitimately fail to commit, returned
// as std::nullopt (pending) rather than a false definite digit.
//
// The extractor refines by pulling more series terms until the interval
// is narrow enough OR a step budget is exhausted (honest pending).
#ifndef NAM_REFINE_HPP
#define NAM_REFINE_HPP

#include <cstdint>
#include <optional>
#include <vector>

#include "nam/big_int.hpp"
#include "nam/series.hpp"

namespace nam
{
    // A bounded interval [lo/den, hi/den] for the FRACTIONAL part of the
    // value being extracted. integer_part holds digits already consumed to
    // the left of (and including) those already emitted; lo/hi/den track the
    // remaining unresolved fraction scaled into [0, 1).
    struct DigitExtractor
    {
        SeriesVM vm;
        uint32_t base = 10;
        // Remaining value interval, tracked as integer numerators over a
        // common positive denominator `scale`. Invariant: 0 <= lo <= hi and
        // the true remaining value v satisfies lo/scale <= v < hi/scale,
        // with hi - lo < scale always achievable by refining.
        BigInt lo; // lower numerator
        BigInt hi; // upper numerator
        BigInt scale; // common denominator (positive)
        // How many series terms to advance per refinement attempt.
        int refine_terms_per_step = 1;
        // Max refinement iterations before honestly reporting pending.
        int max_refine_iters = 4096;
        // Number of digits already committed (in `base`). The fractional
        // remainder equals base^consumed * full_fraction - prefix_value.
        int consumed = 0;

        // Refresh [lo, hi] over `scale` from the current series interval.
        // value in [num/den, (num + err)/den]; we represent that over
        // scale = den so lo = num, hi = num + err.
        void sync_interval()
        {
            BigInt err = vm.tail();
            scale = vm.den;
            // Shift the full-fraction interval left by `consumed` base-digits,
            // then subtract the integer prefix already emitted. This re-derives
            // the remaining fraction exactly from the VM (interval-honest),
            // avoiding the broken incremental rescale.
            BigInt shift = big_pow(BigInt(static_cast<int64_t>(base)),
                                   static_cast<uint64_t>(consumed));
            BigInt full_lo = vm.num * shift;
            BigInt full_hi = (vm.num + err) * shift;
            // prefix * scale subtracts the integer digits already emitted.
            BigInt prefix_scaled = prefix * scale;
            lo = full_lo - prefix_scaled;
            hi = full_hi - prefix_scaled;
        }

        // Width of the interval as hi - lo (over scale). Caller compares.
        BigInt width() const { return hi - lo; }
        // Integer value of digits already emitted, as a big integer in `base`.
        BigInt prefix;
    };

    // Build an extractor for a fractional value in [0, 1) from a series VM.
    // The caller is responsible for ensuring the series converges to a value
    // in [0, 1); for whole transcendentals like e we extract the fractional
    // part separately (see constants.hpp).
    inline DigitExtractor make_extractor(SeriesVM vm, uint32_t base)
    {
        DigitExtractor ex;
        ex.vm = std::move(vm);
        ex.base = base;
        ex.sync_interval();
        return ex;
    }

    // Emit one base-`base` digit if it can be committed honestly. Returns
    // nullopt if the interval cannot yet distinguish the digit within the
    // refinement budget (pending), e.g. exact-boundary inputs.
    //
    // Mechanism: scale the fractional interval by base. The digit is
    // floor(base * lo / scale) and is committable iff
    // floor(base * lo / scale) == floor(base * (hi) / scale) where hi is the
    // (exclusive-ish) upper bound. We refine the series until that holds.
    inline std::optional<uint32_t> next_digit(DigitExtractor& ex)
    {
        BigInt B = BigInt(static_cast<int64_t>(ex.base));
        for (int iter = 0; iter < ex.max_refine_iters; ++iter)
        {
            // Always work from a freshly-synced interval so the bounds are
            // exact for the current series depth (no incremental rescale).
            ex.sync_interval();
            // floor(base * lo / scale) and floor(base * hi / scale).
            BigInt lo_scaled = B * ex.lo;
            BigInt hi_scaled = B * ex.hi;
            BigInt r;
            BigInt dlo = BigInt::floordiv(lo_scaled, ex.scale, r);
            BigInt dhi = BigInt::floordiv(hi_scaled, ex.scale, r);
            if (dlo == dhi)
            {
                // Commit digit dlo. Record it in the running prefix so the
                // next sync_interval() re-derives the remaining fraction.
                ex.prefix = ex.prefix * B + dlo;
                ++ex.consumed;
                return static_cast<uint32_t>(dlo.to_i64());
            }
            // Refine: pull more series terms to shrink [lo, hi], then loop
            // (the top of the loop re-syncs from the deeper series state).
            for (int t = 0; t < ex.refine_terms_per_step; ++t)
            {
                ex.vm.step_term();
            }
        }
        return std::nullopt; // honest pending
    }

    // Extract up to `n` digits, refining to a target width first so digit
    // commitment is robust. Returns the digits actually committed (may be
    // fewer than n if a boundary value stalls -- honest pending).
    inline std::vector<uint32_t> extract_digits(DigitExtractor ex, int n)
    {
        std::vector<uint32_t> out;
        out.reserve(n);
        for (int i = 0; i < n; ++i)
        {
            auto d = next_digit(ex);
            if (!d.has_value()) break;
            out.push_back(*d);
        }
        return out;
    }
} // namespace nam

#endif // NAM_REFINE_HPP
