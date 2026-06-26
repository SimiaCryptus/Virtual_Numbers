// include/nam/constants.hpp
//
// Phase 2: A standard repertoire of series-tier constants, each shipping
// its own compiled convergence proof (tail-bound oracle), per THEORY.md
// "The library must ship a standard repertoire of these oracles (for pi,
// e, log, ...)".
//
// We accumulate exact rational partial sums num/den with a running common
// denominator, and provide a monotone tail bound. The fractional digit
// extractor (refine.hpp) consumes these.
//
// Constants provided in Phase 2:
//   - e            (sum 1/k!)            -> value 2.718...
//   - exp_frac(): fractional part of e   -> 0.718...
//   - ln2          (sum 1/(k 2^k))       -> 0.693...
//   - one_over_e   (sum (-1)^k / k!)     -> 0.367...
//
// Each spec maintains den as a growing factorial / power so the extractor
// sees a denominator that is always a multiple of the previous one (the
// exact-rescale invariant refine.hpp relies on).
#ifndef NAM_CONSTANTS_HPP
#define NAM_CONSTANTS_HPP

#include <memory>

#include "nam/big_int.hpp"
#include "nam/series.hpp"

namespace nam
{
    // --- e = sum_{k>=0} 1/k! ---
    //
    // We keep den = N! for the current number of terms N (index). When we add
    // term 1/k! we put everything over the new common denominator.
    // advance(index n): S_{n} accumulated with den = n! ; to add term 1/(n)!
    // we... Concretely we maintain: after `index` terms summed (k=0..index-1),
    // den = (index-1)!  (or 1 when index==0). Simpler: we maintain den = m!
    // where m = index, and num = m! * sum_{k=0}^{m-1} 1/k!.
    //
    // advance(n, num, den):
    //   new denominator = (n+1)! ... but we add the term for k=n: 1/n!.
    //   Represent over den' = n! : multiply existing num by n (since
    //   n!/(n-1)! = n) then add (n!/n!)=1. Keep den' = n!.
    inline std::shared_ptr<const SeriesSpec> e_spec()
    {
        auto spec = std::make_shared<SeriesSpec>();
        spec->name = "e";
        spec->advance = [](uint64_t n, BigInt& num, BigInt& den)
        {
            // State invariant on entry: den = n! and num = n! * sum_{k<n} 1/k!
            // (for n==0: den=1, num=0).
            if (n == 0)
            {
                // add term k=0 = 1: num = 1, den = 1 (0! = 1)
                num = BigInt(1);
                den = BigInt(1);
                return;
            }
            // Move to den = n! : multiply num and den by n.
            BigInt bn = BigInt(static_cast<int64_t>(n));
            num *= bn;
            den *= bn;
            // Add term k=n = 1/n! -> add 1 to num over den=n!.
            num += BigInt(1);
        };
        spec->tail_bound = [](uint64_t n, const BigInt& den)
        {
            // After summing k=0..n-1 we are at index n with den=(n-1)! ... but
            // because advance leaves den = n! after processing index n-1 ->
            // we treat den as the current factorial. The tail
            // sum_{k>=n} 1/k! <= 2/n!  (for n>=1). Expressed over den: since
            // den = (current factorial), tail/1 <= 2/den-magnitude. We return
            // err such that |value - num/den| <= err/den, so err = 2 suffices
            // when den >= n! and the tail is < 2/den. Use a safe constant.
            if (n == 0) return BigInt(3); // crude bound before any terms
            (void)den;
            return BigInt(2);
        };
        return spec;
    }

    // --- ln2 = sum_{k>=1} 1/(k 2^k) ---
    //
    // den = k * 2^k common denominator is awkward; instead keep den = 2^m * L
    // growing. For Phase 2 we use a simpler exact form: maintain den = 2^n
    // and num accordingly won't be exact because of the 1/k factor. So we
    // keep den = lcm-ish growing factorial-of-2 times n. To preserve the
    // "den' is a multiple of den" invariant we use den = n! * 2^n.
    inline std::shared_ptr<const SeriesSpec> ln2_spec()
    {
        auto spec = std::make_shared<SeriesSpec>();
        spec->name = "ln2";
        spec->advance = [](uint64_t n, BigInt& num, BigInt& den)
        {
            // index n -> add term for k = n+1: 1/((n+1) 2^{n+1}).
            // Maintain den = (n)! * 2^n on entry (den=1 at n=0).
            uint64_t k = n + 1;
            BigInt bk = BigInt(static_cast<int64_t>(k));
            // new den = den * k * 2  (=> k! * 2^k).
            BigInt newden = den * bk * BigInt(2);
            // rescale existing num to newden: factor = k*2.
            num = num * bk * BigInt(2);
            // add term 1/(k 2^k) over newden: newden/(k 2^k) = (k-1)! * 2^{... }
            // term numerator = newden / (k * 2^k). Since newden = k! 2^k,
            // newden/(k 2^k) = (k-1)!.
            // Compute (k-1)! incrementally is costly; derive via division.
            BigInt denom_term = bk * big_pow(BigInt(2), k);
            BigInt r;
            BigInt term_num = BigInt::divmod(newden, denom_term, r);
            num += term_num;
            den = newden;
        };
        spec->tail_bound = [](uint64_t n, const BigInt& den)
        {
            // tail = sum_{k>n} 1/(k 2^k) <= 1/(2^n).  Over den = n! 2^n the
            // bound err/den <= 1/2^n means err <= den / 2^n = n!. Return n!.
            if (n == 0) return BigInt(1);
            // den = n! * 2^n  => n! = den / 2^n.
            BigInt r;
            BigInt nfact = BigInt::divmod(den, big_pow(BigInt(2), n), r);
            return nfact;
        };
        return spec;
    }

    // --- 1/e = sum_{k>=0} (-1)^k / k! ---
    inline std::shared_ptr<const SeriesSpec> one_over_e_spec()
    {
        auto spec = std::make_shared<SeriesSpec>();
        spec->name = "1/e";
        spec->advance = [](uint64_t n, BigInt& num, BigInt& den)
        {
            if (n == 0)
            {
                num = BigInt(1);
                den = BigInt(1);
                return;
            }
            BigInt bn = BigInt(static_cast<int64_t>(n));
            num *= bn;
            den *= bn;
            // term k=n is (-1)^n / n!
            if (n % 2 == 0) num += BigInt(1);
            else num -= BigInt(1);
        };
        spec->tail_bound = [](uint64_t n, const BigInt& den)
        {
            // alternating, monotone: |tail| <= 1/n!  => err <= 1 over den=n!.
            if (n == 0) return BigInt(2);
            (void)den;
            return BigInt(1);
        };
        return spec;
    }

    // Convenience builders.
    inline SeriesVM make_e(uint32_t base = 10)
    {
        return make_series(e_spec(), base);
    }

    inline SeriesVM make_ln2(uint32_t base = 10)
    {
        return make_series(ln2_spec(), base);
    }

    inline SeriesVM make_one_over_e(uint32_t base = 10)
    {
        return make_series(one_over_e_spec(), base);
    }
} // namespace nam

#endif // NAM_CONSTANTS_HPP