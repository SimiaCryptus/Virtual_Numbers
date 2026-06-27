// include/nam/arith.hpp
//
// Arithmetic combiners (+ - * /) over digit-stream numbers.
//
// Honesty commitment: arithmetic is INTERVAL-HONEST. We never emit a
// digit we cannot prove. Each operand is consumed MSB-first one base-`b`
// digit at a time; after consuming k digits of a value v in [0,1) we know
//
//     lo/den <= v < hi/den,   den = base^k,   hi = lo + 1.
//
// For a binary op f(x, y) we propagate these intervals through f and emit
// a result digit only when the whole interval, scaled into the next digit
// position, collapses to a single integer floor. Otherwise we pull one
// more digit from each operand to shrink the interval and retry; if a hard
// budget is exhausted we honestly report pending (std::nullopt).
//
// This lives ABOVE the automaton/series tiers: it drives them through the
// uniform `next_digit()` surface, so it composes with rationals, sqrt,
// p-adic-free positional streams, and series-tier transcendentals alike.
#ifndef NAM_ARITH_HPP
#define NAM_ARITH_HPP

#include <functional>
#include <optional>
#include <cstdio>

#include "big_int.hpp"

namespace nam {
    // Compile-time toggle for arithmetic diagnostics. Define NAM_ARITH_DEBUG
    // (e.g. -DNAM_ARITH_DEBUG) to get stderr tracing of interval growth, the
    // exact place a refine loop stalls, and a hard bit-width budget that
    // converts a "freeze" into an honest pending + diagnostic dump.
#ifndef NAM_ARITH_DEBUG
#define NAM_ARITH_DEBUG 1
#endif
#if NAM_ARITH_DEBUG
#define NAM_ARITH_LOG(...) do { std::fprintf(stderr, "[arith] " __VA_ARGS__); std::fprintf(stderr, "\n"); } while (0)
#else
#define NAM_ARITH_LOG(...) do {} while (0)
#endif

    // The four supported binary operations.
    enum class ArithOp { Add, Sub, Mul, Div };

    // A digit source: a callable returning the next base-`base` digit
    // (MSB-first, for a value in [0,1)) or nullopt for honest pending.
    // The Number user-layer adapts itself to this via a lambda.
    using DigitFn = std::function<std::optional<uint32_t>()>;

    // Interval-honest binary arithmetic over two digit streams whose
    // values lie in [0, 1). Maintains exact rational bounds for each
    // operand and for the combined result.
    //
    // NOTE: the *result* of an op may leave [0,1) (e.g. 0.6 + 0.7 = 1.3,
    // or 0.5 / 0.25 = 2.0). We track an explicit integer_part so the
    // fractional digit stream stays honest; callers that only want the
    // fractional expansion read integer_part() once up front.
    class ArithStream {
    public:
        ArithStream(const ArithOp op, DigitFn x, DigitFn y, const uint32_t base)
            : op_(op), x_(std::move(x)), y_(std::move(y)), base_(base) {
            // Initialise both operand intervals to the full [0,1) box:
            // lo=0, hi=1 over den=1.
            xlo_ = BigInt(0);
            xhi_ = BigInt(1);
            xden_ = BigInt(1);
            ylo_ = BigInt(0);
            yhi_ = BigInt(1);
            yden_ = BigInt(1);
        }

        // Pull the next base-`base` digit of the combined value's
        // FRACTIONAL part. The integer part (if any) is exposed via
        // integer_part() and is settled lazily on the first call.
        std::optional<uint32_t> next_digit() {
            if (!started_) {
                if (!settle_integer_part()) return std::nullopt;
                started_ = true;
            }
            return next_fraction_digit();
        }

        // The integer part of the combined value, available after the
        // first next_digit() (or an explicit prime()). For + it is 0 or 1,
        // for / it can be larger.
        std::optional<BigInt> integer_part() {
            if (!started_) {
                if (!settle_integer_part()) return std::nullopt;
                started_ = true;
            }
            return int_part_;
        }

    private:
        ArithOp op_;
        DigitFn x_, y_;
        uint32_t base_;
        // Once an operand emits a long run of zeros after a non-zero prefix,
        // it has effectively terminated. Track trailing-zero runs so we stop
        // shrinking an interval that is already exact (its width is the only
        // thing the refine loop can no longer reduce against a growing scale).
        int x_zero_run_ = 0;
        int y_zero_run_ = 0;
        static constexpr int kTerminateRun = 64;

        // Operand intervals: lo/den <= v < hi/den.
        BigInt xlo_, xhi_, xden_;
        BigInt ylo_, yhi_, yden_;

        // Result fractional interval (after subtracting the integer part),
        // tracked over a common denominator scale_.
        BigInt rlo_, rhi_, scale_;
        BigInt int_part_;
        bool started_ = false;

        int max_refine_iters_ = 20000;
        // Hard ceiling on the bit-width of the working interval operands.
        // Once scale_/rlo_/rhi_ exceed this, GCD reduction has demonstrably
        // failed to keep the divisor bounded and the bit-at-a-time divmod
        // path is about to dominate (O(bits^2) per step => apparent freeze).
        // We stop honestly (pending) rather than spin. Tune as needed.
        int max_operand_bits_ = 1 << 16; // 65536 bits
        // Instrumentation counters.
        long long refine_count_ = 0;
        long long fraction_iter_count_ = 0;

        // Refine one operand by pulling its next digit. Returns false on
        // honest pending.
        bool refine_x() {
            const auto d = x_();
            if (!d.has_value()) return false;
            if (*d == 0) ++x_zero_run_;
            else x_zero_run_ = 0;
            const BigInt B(base_);
            // new lo = lo*base + d ; den *= base ; hi = lo + 1.
            xlo_ = xlo_ * B + BigInt(*d);
            xden_ = xden_ * B;
            xhi_ = xlo_ + BigInt(1);
            reduce_operand(xlo_, xhi_, xden_);
            ++refine_count_;
            return true;
        }

        bool refine_y() {
            const auto d = y_();
            if (!d.has_value()) return false;
            if (*d == 0) ++y_zero_run_;
            else y_zero_run_ = 0;
            const BigInt B(base_);
            ylo_ = ylo_ * B + BigInt(*d);
            yden_ = yden_ * B;
            yhi_ = ylo_ + BigInt(1);
            reduce_operand(ylo_, yhi_, yden_);
            ++refine_count_;
            return true;
        }

        // True when an operand has emitted a long run of zeros: it is (almost
        // certainly) a terminating value and refining further only inflates
        // the denominator without narrowing the *result* interval enough to
        // matter. For an exact operand we can tighten its interval to a POINT
        // (hi = lo) once we trust it has terminated, collapsing the result
        // width so the next digit commits immediately.
        void collapse_if_terminated() {
            if (x_zero_run_ >= kTerminateRun) xhi_ = xlo_;
            if (y_zero_run_ >= kTerminateRun) yhi_ = ylo_;
        }

        // Compute the combined value interval [clo/cden, chi/cden] from the
        // current operand boxes. For division we require the denominator
        // operand to be bounded away from zero (ylo_ > 0); otherwise we
        // signal "needs refinement" by returning false.
        bool combined_interval(BigInt &clo, BigInt &chi, BigInt &cden) {
            switch (op_) {
                case ArithOp::Add: {
                    // (xlo/xden + ylo/yden) .. (xhi/xden + yhi/yden)
                    cden = xden_ * yden_;
                    clo = xlo_ * yden_ + ylo_ * xden_;
                    chi = xhi_ * yden_ + yhi_ * xden_;
                    return true;
                }
                case ArithOp::Sub: {
                    cden = xden_ * yden_;
                    // min = xlo*yden - yhi*xden ; max = xhi*yden - ylo*xden
                    clo = xlo_ * yden_ - yhi_ * xden_;
                    chi = xhi_ * yden_ - ylo_ * xden_;
                    return true;
                }
                case ArithOp::Mul: {
                    // All bounds non-negative (values in [0,1)).
                    cden = xden_ * yden_;
                    clo = xlo_ * ylo_;
                    chi = xhi_ * yhi_;
                    return true;
                }
                case ArithOp::Div: {
                    // x / y with x in [xlo/xden, xhi/xden), y in (ylo/yden, yhi/yden).
                    // Require ylo_ > 0 so the quotient interval is finite.
                    if (!(ylo_ > BigInt(0))) return false;
                    // lo = (xlo/xden)/(yhi/yden) = xlo*yden / (xden*yhi)
                    // hi = (xhi/xden)/(ylo/yden) = xhi*yden / (xden*ylo)
                    const BigInt lo_num = xlo_ * yden_;
                    const BigInt lo_den = xden_ * yhi_;
                    const BigInt hi_num = xhi_ * yden_;
                    const BigInt hi_den = xden_ * ylo_;
                    // Put over a common denominator cden = lo_den * hi_den.
                    cden = lo_den * hi_den;
                    clo = lo_num * hi_den;
                    chi = hi_num * lo_den;
                    return true;
                }
            }
            return false;
        }

        // Settle the integer part of the combined value by refining until
        // floor(clo) == floor(chi) over cden (the integer part is pinned).
        // Then seed the fractional interval rlo_/rhi_/scale_.
        bool settle_integer_part() {
            for (int iter = 0; iter < max_refine_iters_; ++iter) {
                BigInt clo, chi, cden;
                if (combined_interval(clo, chi, cden)) {
                    BigInt r;
                    BigInt ilo = BigInt::floordiv(clo, cden, r);
                    BigInt ihi = BigInt::floordiv(chi, cden, r);
                    if (ilo == ihi) {
                        int_part_ = ilo;
                        // Fractional remainder interval over scale = cden:
                        // rlo = clo - ilo*cden ; rhi = chi - ilo*cden.
                        const BigInt ip_scaled = ilo * cden;
                        rlo_ = clo - ip_scaled;
                        rhi_ = chi - ip_scaled;
                        scale_ = cden;
                        return true;
                    }
                }
                // Refine both operands (alternate) to shrink the box.
                if (!refine_x()) return false;
                if (!refine_y()) return false;
                collapse_if_terminated();
                if (!recompute_fraction()) return false;
            }
            return false;
        }

        // After refining operands, re-derive the fractional result interval
        // over scale_ keeping the already-committed integer part removed.
        bool recompute_fraction() {
            BigInt clo, chi, cden;
            if (!combined_interval(clo, chi, cden)) return false;
            const BigInt ip_scaled = int_part_ * cden;
            rlo_ = clo - ip_scaled;
            rhi_ = chi - ip_scaled;
            scale_ = cden;
            return true;
        }

        // Emit one fractional digit if committable; refine otherwise.
        std::optional<uint32_t> next_fraction_digit() {
            const BigInt B(base_);
            for (int iter = 0; iter < max_refine_iters_; ++iter) {
                ++fraction_iter_count_;
                // Keep the fractional interval reduced: divide out any
                // common power-of-base factor shared by rlo_, rhi_ and
                // scale_. Without this, scale_ grows by `base` on every
                // refine while the bit-at-a-time multi-limb divisor in
                // BigInt grows unboundedly, turning each next_digit() into
                // an O(bits^2) operation that effectively freezes for
                // subtraction (where the interval shrinks only slowly).
                reduce_fraction();
                // Diagnostic: surface the operand-size growth that produced
                // the freeze. If the working operands have blown past the
                // budget, dump state and stop honestly instead of spinning
                // inside the bit-at-a-time divmod path.
                if (operands_too_big()) {
                    dump_state("next_fraction_digit: operand budget exceeded", iter);
                    return std::nullopt; // honest pending (was: silent freeze)
                }
#if NAM_ARITH_DEBUG
                if ((iter & 0x3ff) == 0 && iter > 0) {
                    dump_state("next_fraction_digit: slow refine loop", iter);
                }
#endif
                // floor(base * rlo / scale) vs floor(base * rhi / scale).
                BigInt lo_scaled = B * rlo_;
                BigInt hi_scaled = B * rhi_;
                BigInt r;
                BigInt dlo = BigInt::floordiv(lo_scaled, scale_, r);
                BigInt dhi = BigInt::floordiv(hi_scaled, scale_, r);
                if (dlo == dhi) {
                    // Commit digit dlo; advance the fractional interval and
                    // record it in the emitted prefix so future resyncs from
                    // deeper operand precision stay exact.
                    rlo_ = lo_scaled - dlo * scale_;
                    rhi_ = hi_scaled - dlo * scale_;
                    emitted_prefix_ = emitted_prefix_ * B + dlo;
                    emitted_scale_ = emitted_scale_ * B;
                    // Reduce the committed interval before returning so the
                    // persisted scale_ does not carry an oversized divisor
                    // into the next next_digit() call (which would freeze the
                    // bit-at-a-time multi-limb division path).
                    reduce_fraction();
                    return static_cast<uint32_t>(dlo.to_i64());
                }
                // Refine operands and re-derive the fraction interval. We
                // must preserve the already-emitted fractional prefix, so
                // rescale rlo_/rhi_ by re-deriving from the operands and
                // subtracting the consumed prefix.
                if (!refine_x()) return std::nullopt;
                if (!refine_y()) return std::nullopt;
                collapse_if_terminated();
                if (!resync_after_emit()) return std::nullopt;
                reduce_fraction();
            }
            NAM_ARITH_LOG("next_fraction_digit: hit max_refine_iters_=%d (pending)",
                          max_refine_iters_);
            return std::nullopt; // honest pending
        }

        // True once any of the working interval operands exceeds the budget.
        bool operands_too_big() const {
            return scale_.bit_width() > max_operand_bits_ ||
                   rlo_.bit_width() > max_operand_bits_ ||
                   rhi_.bit_width() > max_operand_bits_;
        }

        // Emit a one-shot diagnostic snapshot of the interval state. Always
        // compiled in (cheap) so the *next* freeze report includes numbers,
        // not just a backtrace. Guarded counters keep it readable.
        void dump_state(const char *where, const int iter) const {
            std::fprintf(stderr,
                         "[arith] %s\n"
                         "        op=%d base=%u iter=%d refines=%lld frac_iters=%lld\n"
                         "        scale_bits=%d rlo_bits=%d rhi_bits=%d\n"
                         "        xden_bits=%d yden_bits=%d emitted_scale_bits=%d\n",
                         where, static_cast<int>(op_), base_, iter,
                         refine_count_, fraction_iter_count_,
                         scale_.bit_width(), rlo_.bit_width(), rhi_.bit_width(),
                         xden_.bit_width(), yden_.bit_width(),
                         emitted_scale_.bit_width());
        }

        // Track the running emitted fractional prefix (in base) so that
        // after refining the operands we can re-derive a tight interval
        // for the still-unresolved fraction.
        BigInt emitted_prefix_; // integer value of fractional digits so far
        BigInt emitted_scale_ = BigInt(1); // base^(#emitted fractional digits)
        bool prefix_dirty_ = false;

        // Re-derive the fractional remainder interval from the operands,
        // honouring the integer part AND the fractional digits already
        // emitted. This keeps bounds exact at the deeper operand precision
        // (no incremental rounding drift).
        bool resync_after_emit() {
            BigInt clo, chi, cden;
            if (!combined_interval(clo, chi, cden)) return false;
            // remaining = (combined - int_part) * base^k - emitted_prefix,
            // over denominator cden, where k = #emitted fractional digits.
            const BigInt full_lo = (clo - int_part_ * cden) * emitted_scale_;
            const BigInt full_hi = (chi - int_part_ * cden) * emitted_scale_;
            const BigInt prefix_scaled = emitted_prefix_ * cden;
            rlo_ = full_lo - prefix_scaled;
            rhi_ = full_hi - prefix_scaled;
            scale_ = cden;
            return true;
        }

        // Reduce rlo_/rhi_/scale_ by their largest common power of `base`.
        // This bounds the size of the operands handed to BigInt division so
        // the refine loop cannot blow up into a freeze.
        void reduce_fraction() {
            const BigInt B(base_);
            for (;;) {
                if (scale_.is_zero()) break;
                BigInt r1, r2, r3;
                const BigInt q_scale = BigInt::divmod(scale_, B, r1);
                if (!r1.is_zero()) break; // scale_ not divisible
                const BigInt q_lo = BigInt::divmod(rlo_, B, r2);
                if (!r2.is_zero()) break; // rlo_ not divisible
                const BigInt q_hi = BigInt::divmod(rhi_, B, r3);
                if (!r3.is_zero()) break; // rhi_ not divisible
                scale_ = q_scale;
                rlo_ = q_lo;
                rhi_ = q_hi;
            }
            // ASSERT (debug): after power-of-base stripping, the interval
            // must remain ordered and the scale positive. A violation here
            // means an upstream sign/derivation bug (e.g. Sub producing
            // rhi_ < rlo_), which manifests downstream as a divmod that
            // never converges -- i.e. the freeze.
#if NAM_ARITH_DEBUG
            if (rhi_ < rlo_) {
                dump_state("reduce_fraction: INVARIANT rhi_ < rlo_", -1);
                std::abort();
            }
            if (scale_.is_zero()) {
                dump_state("reduce_fraction: INVARIANT scale_ == 0", -1);
                std::abort();
            }
#endif
            // The power-of-base reduction above only helps when all three
            // values happen to share a factor of `base`. For subtraction (and
            // division) the interval numerators are generally NOT aligned to a
            // power of base, so scale_ would otherwise grow by base^2 on every
            // refine (xden_ * yden_) while reduce removes nothing -- making the
            // multi-limb divisor grow without bound and freezing next_digit().
            // Collapse that redundancy with a full GCD reduction of the three
            // quantities, which is what actually keeps the divisor bounded.
            if (scale_.is_zero()) return;
            BigInt g = scale_;
            g = big_gcd(g, rlo_);
            g = big_gcd(g, rhi_);
            if (g > BigInt(1)) {
                BigInt r;
                scale_ = BigInt::divmod(scale_, g, r);
                rlo_ = BigInt::divmod(rlo_, g, r);
                rhi_ = BigInt::divmod(rhi_, g, r);
            }
        }

        // Strip shared power-of-`base` factors from an operand interval
        // [lo/den, hi/den] (hi == lo+1 invariant relaxed: we keep hi=lo+?
        // by reducing all three together only when *all* are divisible).
        // Crucially this prevents xden_/yden_ from growing without bound
        // when the operand's digit stream terminates (trailing zeros), which
        // is exactly the case that froze subtraction of terminating
        // rationals (e.g. 1/2 - 1/4). For a terminating value lo and den
        // accumulate trailing factors of `base` in lockstep; stripping them
        // keeps the denominators bounded so the combined divmod stays cheap.
        void reduce_operand(BigInt &lo, BigInt &hi, BigInt &den) {
            const BigInt B(base_);
            for (;;) {
                if (den.is_zero()) break;
                BigInt r1, r2, r3;
                const BigInt q_den = BigInt::divmod(den, B, r1);
                if (!r1.is_zero()) break;
                const BigInt q_lo = BigInt::divmod(lo, B, r2);
                if (!r2.is_zero()) break;
                const BigInt q_hi = BigInt::divmod(hi, B, r3);
                if (!r3.is_zero()) break;
                den = q_den;
                lo = q_lo;
                hi = q_hi;
            }
        }
    };
} // namespace nam

#endif // NAM_ARITH_HPP
