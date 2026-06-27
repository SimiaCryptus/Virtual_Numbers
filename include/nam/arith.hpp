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

#include <cstdint>
#include <functional>
#include <optional>

#include "big_int.hpp"

namespace nam
{
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
    class ArithStream
    {
    public:
        ArithStream(ArithOp op, DigitFn x, DigitFn y, uint32_t base)
            : op_(op), x_(std::move(x)), y_(std::move(y)), base_(base)
        {
            // Initialise both operand intervals to the full [0,1) box:
            // lo=0, hi=1 over den=1.
            xlo_ = BigInt(0); xhi_ = BigInt(1); xden_ = BigInt(1);
            ylo_ = BigInt(0); yhi_ = BigInt(1); yden_ = BigInt(1);
        }

        // Pull the next base-`base` digit of the combined value's
        // FRACTIONAL part. The integer part (if any) is exposed via
        // integer_part() and is settled lazily on the first call.
        std::optional<uint32_t> next_digit()
        {
            if (!started_)
            {
                if (!settle_integer_part()) return std::nullopt;
                started_ = true;
            }
            return next_fraction_digit();
        }

        // The integer part of the combined value, available after the
        // first next_digit() (or an explicit prime()). For + it is 0 or 1,
        // for / it can be larger.
        std::optional<BigInt> integer_part()
        {
            if (!started_)
            {
                if (!settle_integer_part()) return std::nullopt;
                started_ = true;
            }
            return int_part_;
        }

    private:
        ArithOp op_;
        DigitFn x_, y_;
        uint32_t base_;

        // Operand intervals: lo/den <= v < hi/den.
        BigInt xlo_, xhi_, xden_;
        BigInt ylo_, yhi_, yden_;

        // Result fractional interval (after subtracting the integer part),
        // tracked over a common denominator scale_.
        BigInt rlo_, rhi_, scale_;
        BigInt int_part_;
        bool started_ = false;

        int max_refine_iters_ = 20000;

        // Refine one operand by pulling its next digit. Returns false on
        // honest pending.
        bool refine_x()
        {
            auto d = x_();
            if (!d.has_value()) return false;
            BigInt B(static_cast<int64_t>(base_));
            // new lo = lo*base + d ; den *= base ; hi = lo + 1.
            xlo_ = xlo_ * B + BigInt(static_cast<int64_t>(*d));
            xden_ = xden_ * B;
            xhi_ = xlo_ + BigInt(1);
            return true;
        }

        bool refine_y()
        {
            auto d = y_();
            if (!d.has_value()) return false;
            BigInt B(static_cast<int64_t>(base_));
            ylo_ = ylo_ * B + BigInt(static_cast<int64_t>(*d));
            yden_ = yden_ * B;
            yhi_ = ylo_ + BigInt(1);
            return true;
        }

        // Compute the combined value interval [clo/cden, chi/cden] from the
        // current operand boxes. For division we require the denominator
        // operand to be bounded away from zero (ylo_ > 0); otherwise we
        // signal "needs refinement" by returning false.
        bool combined_interval(BigInt& clo, BigInt& chi, BigInt& cden)
        {
            switch (op_)
            {
            case ArithOp::Add:
            {
                // (xlo/xden + ylo/yden) .. (xhi/xden + yhi/yden)
                cden = xden_ * yden_;
                clo = xlo_ * yden_ + ylo_ * xden_;
                chi = xhi_ * yden_ + yhi_ * xden_;
                return true;
            }
            case ArithOp::Sub:
            {
                cden = xden_ * yden_;
                // min = xlo*yden - yhi*xden ; max = xhi*yden - ylo*xden
                clo = xlo_ * yden_ - yhi_ * xden_;
                chi = xhi_ * yden_ - ylo_ * xden_;
                return true;
            }
            case ArithOp::Mul:
            {
                // All bounds non-negative (values in [0,1)).
                cden = xden_ * yden_;
                clo = xlo_ * ylo_;
                chi = xhi_ * yhi_;
                return true;
            }
            case ArithOp::Div:
            {
                // x / y with x in [xlo/xden, xhi/xden), y in (ylo/yden, yhi/yden).
                // Require ylo_ > 0 so the quotient interval is finite.
                if (!(ylo_ > BigInt(0))) return false;
                // lo = (xlo/xden)/(yhi/yden) = xlo*yden / (xden*yhi)
                // hi = (xhi/xden)/(ylo/yden) = xhi*yden / (xden*ylo)
                BigInt lo_num = xlo_ * yden_;
                BigInt lo_den = xden_ * yhi_;
                BigInt hi_num = xhi_ * yden_;
                BigInt hi_den = xden_ * ylo_;
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
        bool settle_integer_part()
        {
            for (int iter = 0; iter < max_refine_iters_; ++iter)
            {
                BigInt clo, chi, cden;
                if (combined_interval(clo, chi, cden))
                {
                    BigInt r;
                    BigInt ilo = BigInt::floordiv(clo, cden, r);
                    BigInt ihi = BigInt::floordiv(chi, cden, r);
                    if (ilo == ihi)
                    {
                        int_part_ = ilo;
                        // Fractional remainder interval over scale = cden:
                        // rlo = clo - ilo*cden ; rhi = chi - ilo*cden.
                        BigInt ip_scaled = ilo * cden;
                        rlo_ = clo - ip_scaled;
                        rhi_ = chi - ip_scaled;
                        scale_ = cden;
                        return true;
                    }
                }
                // Refine both operands (alternate) to shrink the box.
                if (!refine_x()) return false;
                if (!refine_y()) return false;
                if (!recompute_fraction()) return false;
            }
            return false;
        }

        // After refining operands, re-derive the fractional result interval
        // over scale_ keeping the already-committed integer part removed.
        bool recompute_fraction()
        {
            BigInt clo, chi, cden;
            if (!combined_interval(clo, chi, cden)) return false;
            BigInt ip_scaled = int_part_ * cden;
            rlo_ = clo - ip_scaled;
            rhi_ = chi - ip_scaled;
            scale_ = cden;
            return true;
        }

        // Emit one fractional digit if committable; refine otherwise.
        std::optional<uint32_t> next_fraction_digit()
        {
            BigInt B(static_cast<int64_t>(base_));
            for (int iter = 0; iter < max_refine_iters_; ++iter)
            {
                // floor(base * rlo / scale) vs floor(base * rhi / scale).
                BigInt lo_scaled = B * rlo_;
                BigInt hi_scaled = B * rhi_;
                BigInt r;
                BigInt dlo = BigInt::floordiv(lo_scaled, scale_, r);
                BigInt dhi = BigInt::floordiv(hi_scaled, scale_, r);
                if (dlo == dhi)
                {
                    // Commit digit dlo; advance the fractional interval and
                    // record it in the emitted prefix so future resyncs from
                    // deeper operand precision stay exact.
                    rlo_ = lo_scaled - dlo * scale_;
                    rhi_ = hi_scaled - dlo * scale_;
                    emitted_prefix_ = emitted_prefix_ * B + dlo;
                    emitted_scale_ = emitted_scale_ * B;
                    return static_cast<uint32_t>(dlo.to_i64());
                }
                // Refine operands and re-derive the fraction interval. We
                // must preserve the already-emitted fractional prefix, so
                // rescale rlo_/rhi_ by re-deriving from the operands and
                // subtracting the consumed prefix.
                if (!refine_x()) return std::nullopt;
                if (!refine_y()) return std::nullopt;
                if (!resync_after_emit()) return std::nullopt;
            }
            return std::nullopt; // honest pending
        }

        // Track the running emitted fractional prefix (in base) so that
        // after refining the operands we can re-derive a tight interval
        // for the still-unresolved fraction.
        BigInt emitted_prefix_;     // integer value of fractional digits so far
        BigInt emitted_scale_ = BigInt(1); // base^(#emitted fractional digits)
        bool   prefix_dirty_ = false;

        // Re-derive the fractional remainder interval from the operands,
        // honouring the integer part AND the fractional digits already
        // emitted. This keeps bounds exact at the deeper operand precision
        // (no incremental rounding drift).
        bool resync_after_emit()
        {
            BigInt clo, chi, cden;
            if (!combined_interval(clo, chi, cden)) return false;
            // remaining = (combined - int_part) * base^k - emitted_prefix,
            // over denominator cden, where k = #emitted fractional digits.
            BigInt full_lo = (clo - int_part_ * cden) * emitted_scale_;
            BigInt full_hi = (chi - int_part_ * cden) * emitted_scale_;
            BigInt prefix_scaled = emitted_prefix_ * cden;
            rlo_ = full_lo - prefix_scaled;
            rhi_ = full_hi - prefix_scaled;
            scale_ = cden;
            return true;
        }
    };
} // namespace nam

#endif // NAM_ARITH_HPP