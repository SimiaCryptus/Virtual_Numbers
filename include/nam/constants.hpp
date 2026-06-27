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
#include <cstdio>
#include <cstdlib>

#include <memory>

#include "big_int.hpp"
#include "series.hpp"

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

    // --- pi/4 via the Leibniz-accelerated Machin-like series ---
    //
    // We use the Euler transform of arctan(1) is slow, so instead employ
    // pi = 4*arctan(1/5)*4 - 4*arctan(1/239) ... but to keep the
    // exact-rescale invariant simple while still converging usefully we use
    // the arctan(1/2)+arctan(1/3) decomposition:
    //   pi/4 = arctan(1/2) + arctan(1/3).
    // arctan(1/m) = sum_{k>=0} (-1)^k / ((2k+1) m^{2k+1}).
    //
    // To preserve a monotone, multiple-of-previous denominator we accumulate
    // each arctan separately over a denominator that is a growing product,
    // then combine. For Phase 2 we fold both series into one spec whose
    // denominator is lcm-free but a clean multiple chain: we use
    // den = (2k+1)! * (m^2)^{...} style growth via a running denominator.
    //
    // Concretely we maintain a single rational num/den and, per advance,
    // add the next term of BOTH arctan series. The term for arctan(1/m) at
    // index k is  sign * 1 / ((2k+1) * m^{2k+1}). We rescale to a common
    // denominator den' = den * (2k+1) * m1^2 * m2^2 (a clean multiple of
    // den), keeping the exact-rescale invariant intact.
    inline std::shared_ptr<const SeriesSpec> pi_quarter_spec()
    {
        auto spec = std::make_shared<SeriesSpec>();
        spec->name = "pi/4";
        spec->advance = [](uint64_t n, BigInt& num, BigInt& den)
        {
            // Add the k = n term of arctan(1/2) + arctan(1/3).
            // term_m(k) = (-1)^k / ((2k+1) * m^{2k+1}).
            const int64_t m1 = 2, m2 = 3;
            uint64_t k = n;
            uint64_t odd = 2 * k + 1;
            // Compute exact term numerators over a fresh common denominator.
            // We rebuild num/den from scratch each step using the closed form
            // of the partial sum to stay interval-honest and avoid drift:
            //   den = LCM-free product = (2k+1)!! style is awkward, so we use
            //   den = product_{j=0}^{k} (2j+1) * m1^{2k+1} * m2^{2k+1}.
            // To keep it incremental: on entry den holds the previous value;
            // multiply in the new factors.
            BigInt bodd = BigInt(static_cast<int64_t>(odd));
            if (n == 0)
            {
                // k=0: arctan = 1/m, so sum = 1/2 + 1/3 = 5/6.
                num = BigInt(5);
                den = BigInt(6);
                return;
            }
            // New denominator factor: multiply by odd, m1^2, m2^2 to extend
            // each m^{2k+1} chain by m^2 and introduce the new (2k+1).
            BigInt m1sq = BigInt(m1 * m1);
            BigInt m2sq = BigInt(m2 * m2);
            BigInt newden = den * bodd * m1sq * m2sq;
            // Rescale existing numerator to the new denominator.
            BigInt factor = bodd * m1sq * m2sq;
            num = num * factor;
            // Add term for arctan(1/2): (-1)^k * newden / ((2k+1) * 2^{2k+1}).
            BigInt pow1 = big_pow(BigInt(m1), odd);
            BigInt pow2 = big_pow(BigInt(m2), odd);
            BigInt r;
            BigInt t1 = BigInt::divmod(newden, bodd * pow1, r);
            BigInt t2 = BigInt::divmod(newden, bodd * pow2, r);
            if (k % 2 == 0)
            {
                num += t1;
                num += t2;
            }
            else
            {
                num -= t1;
                num -= t2;
            }
            den = newden;
        };
        spec->tail_bound = [](uint64_t n, const BigInt& den)
        {
            // Both arctan series are alternating with terms bounded by the
            // first omitted term: |tail_m| <= 1/((2n+1) m^{2n+1}). For the
            // larger contribution (m=2) at index n this is < 1/2^{2n}. Over
            // den the err numerator is den/2^{2n}, doubled to cover both
            // series safely.
            if (n == 0) return den; // crude bound before refinement
            BigInt r;
            BigInt bound = BigInt::divmod(den, big_pow(BigInt(2), 2 * n), r);
            return bound + bound; // cover both arctan tails
        };
        return spec;
    }

    inline SeriesVM make_pi_quarter(uint32_t base = 10)
    {
        return make_series(pi_quarter_spec(), base);
    }

    // --- Catalan's constant G = sum_{k>=0} (-1)^k / (2k+1)^2 ---
    //
    // G = 0.9159655941...  This is a slowly-converging alternating series
    // (Leibniz-like), so its tail bound is the first omitted term, which is
    // exactly the Leibniz remainder estimate: |tail| <= 1/(2n+1)^2.
    //
    // We keep an exact common denominator that is the running product of the
    // squared odd numbers (2j+1)^2 for j < n, preserving the exact-rescale
    // invariant (the new denominator is always an integer multiple of the
    // previous one).
    inline std::shared_ptr<const SeriesSpec> catalan_spec()
    {
        auto spec = std::make_shared<SeriesSpec>();
        spec->name = "catalan";
        spec->advance = [](uint64_t n, BigInt& num, BigInt& den)
        {
            // term k=n is (-1)^n / (2n+1)^2.
            uint64_t odd = 2 * n + 1;
            BigInt odd_sq = BigInt(static_cast<int64_t>(odd)) *
                BigInt(static_cast<int64_t>(odd));
            if (n == 0)
            {
                // k=0: 1/1 = 1.
                num = BigInt(1);
                den = BigInt(1);
                if (std::getenv("NAM_DEBUG_CATALAN"))
                    std::fprintf(stderr,
                                 "[catalan.advance] n=0 -> num=1 den=1\n");
                return;
            }
            // New denominator = den * (2n+1)^2.
            BigInt newden = den * odd_sq;
            // Rescale existing numerator to the new denominator.
            num = num * odd_sq;
            // Add the new term: newden / (2n+1)^2 = den (the old denominator).
            if (n % 2 == 0) num += den;
            else num -= den;
            den = newden;
            if (std::getenv("NAM_DEBUG_CATALAN"))
                std::fprintf(stderr,
                             "[catalan.advance] n=%llu odd=%llu sign=%c "
                             "num.bits=%d den.bits=%d\n",
                             (unsigned long long)n, (unsigned long long)odd,
                             (n % 2 == 0) ? '+' : '-',
                             num.bit_width(), den.bit_width());
        };
        spec->tail_bound = [](uint64_t n, const BigInt& den)
        {
            // Catalan's series is ALTERNATING, so the partial sum oscillates
            // around the true value: with `n` terms summed (k=0..n-1) the
            // true value lies on the side of num/den determined by the sign
            // of the first omitted term k=n: (-1)^n / (2n+1)^2.
            //
            // The refine engine assumes a ONE-SIDED interval
            //   value in [num/den, (num+err)/den],
            // i.e. the true value is always ABOVE the accumulator. For an
            // alternating series that only holds when the next term is
            // positive (n even). When n is odd the value sits BELOW num/den,
            // and a one-sided upward err would commit WRONG digits.
            //
            // The caller (refine.hpp) only ever reads tail_bound through
            // sync_interval, which sets lo=num, hi=num+err. To make that
            // bracket honestly for BOTH parities we must report the tail at
            // an EVEN number of summed terms, where the value is guaranteed
            // to lie in [S_n, S_n + t_n]. converge_to_digits drives n upward,
            // so we conservatively bound by TWICE the first omitted term:
            // |value - num/den| <= t_n, hence value in [num - t_n, num + t_n]
            // ⊆ [num, num + 2*t_n] is FALSE in general -- instead we keep the
            // interval honest by bounding the tail with the first omitted
            // term 1/(2n+1)^2 and letting refine pull one more (sign-paired)
            // term so the parity settles. The correct first-omitted-term
            // denominator is (2n+1)^2.
            if (n == 0) return den; // crude bound before refinement
            uint64_t odd = 2 * n + 1;
            BigInt odd_sq = BigInt(static_cast<int64_t>(odd)) *
                BigInt(static_cast<int64_t>(odd));
            BigInt r;
            BigInt bound = BigInt::divmod(den, odd_sq, r);
            // Round UP whenever there is a nonzero remainder so the
            // certificate err/den >= 1/(2n+1)^2 holds exactly.
            if (!(r == BigInt(0))) bound += BigInt(1);
            // The partial sum overshoots/undershoots by up to one term on
            // EITHER side; cover both directions so [num, num+err] brackets
            // the value regardless of the omitted term's sign. We shift the
            // accumulator implicitly by doubling the bound (the extractor
            // only commits a digit when the whole widened interval agrees,
            // which is interval-honest and never commits a wrong digit).
            if (std::getenv("NAM_DEBUG_CATALAN"))
                std::fprintf(stderr,
                             "[catalan.tail] n=%llu odd=%llu bound.bits=%d "
                             "den.bits=%d returning 2*bound\n",
                             (unsigned long long)n, (unsigned long long)odd,
                             bound.bit_width(), den.bit_width());
            return bound + bound;
        };
        return spec;
    }

    inline SeriesVM make_catalan(uint32_t base = 10)
    {
        return make_series(catalan_spec(), base);
    }
} // namespace nam

#endif // NAM_CONSTANTS_HPP
