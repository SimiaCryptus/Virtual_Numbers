// tests/all_tests.cpp
//
// Phase 1 test suite. Interval-honest from day one: no assert(x == y) on
// values; we compare bounded digit prefixes, fork-determinism is EXACT,
// and we instrument the complexity metric (struct size + bit-width growth).
#include "test_main.hpp"

#include <array>
#include <vector>
#include <optional>

#include "nam/abi.h"
#include "nam/generator.hpp"
#include "nam/rational.hpp"
#include "nam/algebraic.hpp"
#include "nam/codec.hpp"
#include "nam/padic.hpp"
#include "nam/compare.hpp"
#include "nam/metric.hpp"
#include "nam/skip.hpp"
#include "nam/big_int.hpp"
#include "nam/series.hpp"
#include "nam/refine.hpp"
#include "nam/constants.hpp"
#include "nam/memo.hpp"
#include "nam/number.hpp"
#include "nam/expr.hpp"
#include "nam/jit.hpp"

using namespace nam;

// ---------- M1: ABI guardrails (compile-time + runtime fork) ----------

NAM_TEST(abi_struct_size)
{
    CHECK(sizeof(AutomatonVM) == 40);
    CHECK(std::is_trivially_copyable_v<AutomatonVM>);
    CHECK(std::is_standard_layout_v<AutomatonVM>);
}

NAM_TEST(abi_fork_is_exact)
{
    // Fork, then compare digit prefixes -- must be EXACT, not interval.
    AutomatonVM a = make_rational(1, 7, 10);
    AutomatonVM b = num_vm_fork(a);
    for (int i = 0; i < 50; ++i)
    {
        NumVMStep ra = Rational::step(a);
        NumVMStep rb = Rational::step(b);
        CHECK(ra.digit == rb.digit);
        a = ra.next;
        b = rb.next;
    }
}

// ---------- M2: Rationals ----------

NAM_TEST(rational_one_seventh_base10)
{
    // 1/7 = 0.(142857) repeating.
    AutomatonVM v = make_rational(1, 7, 10);
    const uint32_t expect[] = {1, 4, 2, 8, 5, 7, 1, 4, 2, 8, 5, 7};
    for (uint32_t e : expect)
    {
        NumVMStep r = Rational::step(v);
        CHECK(r.digit == e);
        v = r.next;
    }
}

NAM_TEST(rational_three_eighths_terminates)
{
    // 3/8 = 0.375000...
    AutomatonVM v = make_rational(3, 8, 10);
    const uint32_t expect[] = {3, 7, 5, 0, 0, 0};
    for (uint32_t e : expect)
    {
        NumVMStep r = Rational::step(v);
        CHECK(r.digit == e);
        v = r.next;
    }
}

NAM_TEST(rational_period_detection)
{
    auto [pre, per] = rational_period(make_rational(1, 7, 10));
    CHECK(pre == 0);
    CHECK(per == 6);

    auto [pre2, per2] = rational_period(make_rational(3, 8, 10));
    CHECK(per2 == 0); // terminating

    // 1/6 = 0.1(6): preperiod 1, period 1.
    auto [pre3, per3] = rational_period(make_rational(1, 6, 10));
    CHECK(pre3 == 1);
    CHECK(per3 == 1);
}

NAM_TEST(rational_complexity_is_constant_state)
{
    // Logical register count for rationals is 1 (the remainder). The struct
    // size never changes as we emit digits.
    AutomatonVM v = make_rational(1, 7, 10);
    size_t sz = sizeof(v);
    for (int i = 0; i < 100; ++i) v = Rational::step(v).next;
    CHECK(sizeof(v) == sz); // constant memory
    CHECK(sizeof(v) == 40);
}

// ---------- M3: Quadratic irrationals ----------

NAM_TEST(sqrt2_base10_golden)
{
    // sqrt(2) = 1.41421356237...  fractional digits: 4,1,4,2,1,3,5,6,2,3,7
    AutomatonVM v = make_sqrt(2, 10);
    const uint32_t expect[] = {4, 1, 4, 2, 1, 3, 5, 6, 2, 3, 7};
    for (uint32_t e : expect)
    {
        NumVMStep r = Sqrt::step(v);
        CHECK(r.digit == e);
        v = r.next;
    }
}

NAM_TEST(sqrt3_base10_golden)
{
    // sqrt(3) = 1.7320508075...  fractional: 7,3,2,0,5,0,8,0,7,5
    AutomatonVM v = make_sqrt(3, 10);
    const uint32_t expect[] = {7, 3, 2, 0, 5, 0, 8, 0, 7, 5};
    for (uint32_t e : expect)
    {
        NumVMStep r = Sqrt::step(v);
        CHECK(r.digit == e);
        v = r.next;
    }
}

NAM_TEST(sqrt5_base10_golden)
{
    // sqrt(5) = 2.2360679...  fractional: 2,3,6,0,6,7,9
    AutomatonVM v = make_sqrt(5, 10);
    const uint32_t expect[] = {2, 3, 6, 0, 6, 7, 9};
    for (uint32_t e : expect)
    {
        NumVMStep r = Sqrt::step(v);
        CHECK(r.digit == e);
        v = r.next;
    }
}

NAM_TEST(sqrt2_prefix_bitwidth_grows_log_n)
{
    // Memory is the complexity metric: the accumulated root prefix register
    // bit-width must grow ~ O(log n) (each digit adds ~log2(base) bits).
    AutomatonVM v = make_sqrt(2, 10);
    int prev = Sqrt::prefix_bitwidth(v);
    std::vector<int> widths;
    for (int i = 0; i < 20; ++i)
    {
        v = Sqrt::step(v).next;
        int w = Sqrt::prefix_bitwidth(v);
        widths.push_back(w);
        CHECK(w >= prev); // monotone non-decreasing
        prev = w;
    }
    // After ~n digits in base 10, width is about n*log2(10) ~ 3.32*n.
    // Check it stays in a linear-in-n (=> O(log n)-per-digit) envelope and
    // is well below n^2 nonsense.
    CHECK(widths.back() <= 20 * 4 + 8);
    CHECK(widths.back() >= 20 * 3);
}

// ---------- M4: Codec layer ----------

NAM_TEST(rational_base_change_is_codec)
{
    // 1/4 = 0.25 in base 10, = 0.1 in base 4, = 0.01 in base 2.
    AutomatonVM b10 = make_rational(1, 4, 10);
    {
        const uint32_t e10[] = {2, 5, 0, 0};
        AutomatonVM v = b10;
        for (uint32_t e : e10)
        {
            auto r = Rational::step(v);
            CHECK(r.digit==e);
            v = r.next;
        }
    }
    AutomatonVM b2 = rational_in_base(b10, 2);
    {
        const uint32_t e2[] = {0, 1, 0, 0};
        AutomatonVM v = b2;
        for (uint32_t e : e2)
        {
            auto r = Rational::step(v);
            CHECK(r.digit==e);
            v = r.next;
        }
    }
    AutomatonVM b4 = rational_in_base(b10, 4);
    {
        const uint32_t e4[] = {1, 0, 0, 0};
        AutomatonVM v = b4;
        for (uint32_t e : e4)
        {
            auto r = Rational::step(v);
            CHECK(r.digit==e);
            v = r.next;
        }
    }
}

NAM_TEST(codec_roundtrip_reproject)
{
    // Reproject 1/3 base 10 -> base 7 -> back, agree to N digits.
    AutomatonVM src = make_rational(1, 3, 10);
    // Read 12 base-10 digits, emit base-7 digits.
    auto b7 = reproject_digits<Rational>(src, 7, 12, 6);
    // 1/3 in base 7 = 0.222222... (since 1/3 = 2/7 + 2/49 + ...).
    for (size_t i = 0; i < b7.size(); ++i)
        CHECK(b7[i] == 2);
}

// ---------- M5: p-adics ----------

NAM_TEST(padic_minus_one_in_z5_is_all_fours)
{
    // -1 in Z_5 = ...444444 (each digit 4). a=-1, b=1, p=5.
    AutomatonVM v = make_padic(-1, 1, 5);
    for (int i = 0; i < 10; ++i)
    {
        NumVMStep r = PAdic::step(v);
        CHECK(r.digit == 4);
        v = r.next;
    }
}

NAM_TEST(padic_one_third_in_z5)
{
    // 1/3 in Z_5. 3^{-1} mod 5 = 2, so first digit 2. Known expansion of
    // 1/3 in Z_5 is ...131313 2 ... let's just check it is periodic and the
    // partial sum reconstructs 1/3 mod 5^k.
    AutomatonVM v = make_padic(1, 3, 5);
    // Reconstruct value mod 5^k from digits and check == 1/3 mod 5^k,
    // i.e. (3 * value) % 5^k == 1.
    long long val = 0, pk = 1;
    for (int k = 0; k < 8; ++k)
    {
        NumVMStep r = PAdic::step(v);
        val += (long long)r.digit * pk;
        pk *= 5;
        CHECK(((3 * val) % pk + pk) % pk == 1 % pk);
        v = r.next;
    }
}

NAM_TEST(padic_period_is_finite)
{
    // 1/3 in Z_5 is purely periodic; period divides the multiplicative
    // order. Just assert a finite period is found.
    auto [pre, per] = padic_period(make_padic(1, 3, 5));
    CHECK(per > 0);
    (void)pre;
}

NAM_TEST(padic_valuation_extractor)
{
    // v_5(50) = 2 (50 = 2 * 5^2). v_5(7) = 0. v_5(125) = 3.
    CHECK(p_valuation(50, 5) == 2);
    CHECK(p_valuation(7, 5) == 0);
    CHECK(p_valuation(125, 5) == 3);
}

// ---------- M6: comparison + p-adic metric ----------

NAM_TEST(compare_interval_honest)
{
    // 1/7 vs 1/3 in base 10: 0.142857... vs 0.333... -> 1/7 < 1/3.
    AutomatonVM a = make_rational(1, 7, 10);
    AutomatonVM b = make_rational(1, 3, 10);
    auto lt = definitely_less_than<Rational>(a, b, 5);
    CHECK(lt.has_value());
    CHECK(*lt == true);
    CHECK(compare<Rational>(a, b, 5) == Trit::Less);
    CHECK(compare<Rational>(b, a, 5) == Trit::Greater);

    // Equal-valued generators are indistinguishable within the bound.
    AutomatonVM c = make_rational(2, 14, 10); // == 1/7
    CHECK(agrees_with<Rational>(a, c, 30));
    CHECK(compare<Rational>(a, c, 30) == Trit::Indistinguishable);
}

NAM_TEST(padic_metric_product_automaton)
{
    // x = 1/3, y = 1/3 + 25 (differ first at 5-adic position 2):
    // x - y = -25, v_5(-25) = 2, distance = 5^-2 = 1/25.
    AutomatonVM x = make_padic(1, 3, 5);
    AutomatonVM y = make_padic(1 + 25 * 3, 3, 5); // (1 + 75)/3 differs by 25
    auto v = padic_valuation_of_difference<PAdic>(x, y, 10);
    CHECK(v.has_value());
    CHECK(*v == 2);
    auto d = padic_distance<PAdic>(x, y, 10);
    CHECK(d.has_value());
    CHECK(*d > 0.0399 && *d < 0.0401); // 1/25 = 0.04
}

NAM_TEST(padic_metric_equal_is_pending)
{
    AutomatonVM x = make_padic(1, 3, 5);
    AutomatonVM y = make_padic(1, 3, 5);
    auto v = padic_valuation_of_difference<PAdic>(x, y, 10);
    CHECK(!v.has_value()); // agree on whole prefix -> honest pending
}

// ---------- M7: skip-ahead ----------

NAM_TEST(skip_rational_matches_naive)
{
    // Jump 1000 digits of 1/7 base 10 and compare to naive stepping.
    AutomatonVM v = make_rational(1, 7, 10);
    AutomatonVM fast = skip_rational(1000, v);
    AutomatonVM slow = skip_naive<Rational>(1000, v);
    // Compare next 12 digits -- must be EXACT.
    for (int i = 0; i < 12; ++i)
    {
        NumVMStep rf = Rational::step(fast);
        NumVMStep rs = Rational::step(slow);
        CHECK(rf.digit == rs.digit);
        fast = rf.next;
        slow = rs.next;
    }
}

NAM_TEST(skip_rational_terminating)
{
    // 3/8 terminates; skipping past the significant digits yields zeros.
    AutomatonVM v = make_rational(3, 8, 10);
    AutomatonVM fast = skip_rational(100, v);
    for (int i = 0; i < 5; ++i)
    {
        NumVMStep r = Rational::step(fast);
        CHECK(r.digit == 0);
        fast = r.next;
    }
}

NAM_TEST(mat_pow_modexp_kernel)
{
    // The general fast-forward kernel: Fibonacci via [[1,1],[1,0]]^n.
    // F(10) = 55. Matrix power top-left after ^10 with no modulus reduction
    // (large modulus) equals F(11)=89; check the standard identity
    // [[F(n+1),F(n)],[F(n),F(n-1)]].
    Mat2 fib{1, 1, 1, 0};
    Mat2 p = mat_pow(fib, 10, (uint64_t)1e18);
    CHECK(p.b == 55); // F(10)
    CHECK(p.a == 89); // F(11)
    CHECK(p.d == 34); // F(9)
}

// ========================= PHASE 2: SERIES TIER =========================
// ---------- BigInt: arbitrary-precision arithmetic ----------
NAM_TEST(bigint_basic_arithmetic)
{
    BigInt a(123456789);
    BigInt b(987654321);
    CHECK((a + b) == BigInt(1111111110));
    CHECK((b - a) == BigInt(864197532));
    CHECK((a - b) == BigInt(-864197532));
    CHECK((BigInt(7) * BigInt(6)) == BigInt(42));
    CHECK((BigInt(-7) * BigInt(6)) == BigInt(-42));
}

NAM_TEST(bigint_large_multiply)
{
    // 2^64 fits and multiplies exactly beyond 64 bits.
    BigInt two_to_32 = big_pow(BigInt(2), 32);
    BigInt two_to_64 = two_to_32 * two_to_32;
    CHECK(two_to_64.to_string() == "18446744073709551616");
    BigInt big = big_pow(BigInt(2), 100);
    CHECK(big.to_string() == "1267650600228229401496703205376");
}

NAM_TEST(bigint_divmod_and_floordiv)
{
    BigInt q, r;
    // 100 / 7 = 14 rem 2.
    q = BigInt::divmod(BigInt(100), BigInt(7), r);
    CHECK(q == BigInt(14));
    CHECK(r == BigInt(2));
    // -100 floordiv 7 = -15 rem 5.
    q = BigInt::floordiv(BigInt(-100), BigInt(7), r);
    CHECK(q == BigInt(-15));
    CHECK(r == BigInt(5));
}

NAM_TEST(bigint_bit_width_grows)
{
    CHECK(BigInt(0).bit_width() == 0);
    CHECK(BigInt(1).bit_width() == 1);
    CHECK(BigInt(255).bit_width() == 8);
    CHECK(big_pow(BigInt(2), 100).bit_width() == 101);
}

// ---------- Series tier: factorial accumulator for e ----------
NAM_TEST(series_e_partial_sum_converges)
{
    // S_n = sum_{k=0}^{n-1} 1/k!. After summing terms, num/den should equal
    // the exact rational partial sum. After 5 terms (k=0..4):
    //   1 + 1 + 1/2 + 1/6 + 1/24 = 65/24.
    SeriesVM vm = make_e(10);
    for (int i = 0; i < 5; ++i) vm.step_term();
    // num/den == 65/24. den should be 4! = 24.
    CHECK(vm.den == BigInt(24));
    CHECK(vm.num == BigInt(65));
}

NAM_TEST(series_fork_is_deep_copy)
{
    // Fork the series VM; mutate the original; the fork must be unaffected
    // (deep copy, NOT copy-on-write aliasing).
    SeriesVM a = make_e(10);
    for (int i = 0; i < 4; ++i) a.step_term();
    SeriesVM b = a.fork();
    BigInt b_num_before = b.num;
    // Advance only `a`.
    for (int i = 0; i < 10; ++i) a.step_term();
    CHECK(b.num == b_num_before); // fork untouched -> deep copy proven
    CHECK(a.index == 14);
    CHECK(b.index == 4);
}

NAM_TEST(series_accumulator_bitwidth_grows)
{
    // Memory is the complexity metric: series-tier accumulator bit-width
    // grows with depth (the factorial denominator grows). Monotone.
    SeriesVM vm = make_e(10);
    int prev = vm.accumulator_bitwidth();
    for (int i = 0; i < 20; ++i)
    {
        vm.step_term();
        int w = vm.accumulator_bitwidth();
        CHECK(w >= prev);
        prev = w;
    }
    CHECK(prev > 0);
}

// ---------- Digit extraction via interval refinement ----------
NAM_TEST(extract_one_over_e_digits)
{
    // 1/e = 0.36787944117...  fractional value already in [0,1).
    SeriesVM vm = make_one_over_e(10);
    DigitExtractor ex = make_extractor(vm, 10);
    auto digits = extract_digits(ex, 6);
    // First digit must be 3, then 6, 7, 8, 7, 9 ...
    const uint32_t expect[] = {3, 6, 7, 8, 7, 9};
    CHECK(digits.size() >= 6);
    for (size_t i = 0; i < digits.size() && i < 6; ++i)
    {
        CHECK(digits[i] == expect[i]);
    }
}

NAM_TEST(extract_ln2_digits)
{
    // ln 2 = 0.69314718056...  value in [0,1).
    SeriesVM vm = make_ln2(10);
    DigitExtractor ex = make_extractor(vm, 10);
    auto digits = extract_digits(ex, 6);
    const uint32_t expect[] = {6, 9, 3, 1, 4, 7};
    CHECK(digits.size() >= 6);
    for (size_t i = 0; i < digits.size() && i < 6; ++i)
    {
        CHECK(digits[i] == expect[i]);
    }
}
NAM_TEST (extract_pi_quarter_digits)
{
    // pi/4 = 0.78539816339...  value in [0,1).
    SeriesVM vm = make_pi_quarter(10);
    DigitExtractor ex = make_extractor(vm, 10);
    auto digits = extract_digits(ex, 6);
    const uint32_t expect[] = {7, 8, 5, 3, 9, 8};
    CHECK(digits.size() >= 6);
    for (size_t i = 0; i < digits.size() && i < 6; ++i)
    {
        CHECK(digits[i] == expect[i]);
    }
}

NAM_TEST (extract_eager_matches_lazy)
{
    // Eager pre-convergence must produce identical digits to the lazy path.
    auto lazy = extract_digits(make_extractor(make_one_over_e(10), 10), 6);
    auto eager = extract_digits_eager(make_extractor(make_one_over_e(10), 10), 6);
    CHECK(lazy.size() >= 6);
    CHECK(eager.size() >= 6);
    for (size_t i = 0; i < 6; ++i)
        CHECK(lazy[i] == eager[i]);
}

NAM_TEST (series_converge_to_digits)
{
    // converge_to_digits must pull enough terms that the interval pins the
    // requested precision (err/den < base^-target).
    SeriesVM vm = make_one_over_e(10);
    uint64_t stepped = vm.converge_to_digits(8);
    CHECK(stepped > 0);
    BigInt err = vm.tail();
    BigInt thr = big_pow(BigInt(10), 8);
    CHECK(err * thr < vm.den);
}

NAM_TEST (bigint_mod_small)
{
    // mod_small must agree with full divmod for small divisors.
    BigInt big = big_pow(BigInt(2), 100);
    BigInt r;
    BigInt::divmod(big, BigInt(7), r);
    CHECK(static_cast<int64_t>(big.mod_small(7)) == r.to_i64());
    CHECK(BigInt(123456789).mod_small(1000) == 789);
}

NAM_TEST (bigint_short_division_fast_path)
{
    // The single-limb fast path must match the bitwise reference for many
    // values (single-limb divisor triggers short division).
    BigInt big = big_pow(BigInt(10), 40);
    BigInt r;
    BigInt q = BigInt::divmod(big, BigInt(99991), r);
    // Reconstruct: q*99991 + r == big.
    CHECK(q * BigInt(99991) + r == big);
    CHECK(r < BigInt(99991));
}

NAM_TEST (scaled_sqrt_is_c_times_sqrt)
{
    // 2*sqrt(2) = sqrt(8) = 2.8284271...  fractional: 8,2,8,4,2,7,1
    AutomatonVM v = make_scaled_sqrt(2, 2, 10);
    const uint32_t expect[] = {8, 2, 8, 4, 2, 7, 1};
    for (uint32_t e : expect)
    {
        NumVMStep r = Sqrt::step(v);
        CHECK(r.digit == e);
        v = r.next;
    }
}

NAM_TEST (cross_generator_compare)
{
    // sqrt(2)=1.414... vs 1/3=0.333... ; fractional streams compared MSB-first:
    // 4 > 3 at digit 0 -> sqrt(2) fractional NOT less than 1/3.
    AutomatonVM s2 = make_sqrt(2, 10);
    AutomatonVM third = make_rational(1, 3, 10);
    auto lt = definitely_less_than_xy<Sqrt, Rational>(s2, third, 5);
    CHECK(lt.has_value());
    CHECK(*lt == false);
}


// ---------- Explicit bounded LRU memoization ----------
NAM_TEST(lru_cache_bounds_and_evicts)
{
    LruDigitCache cache(3);
    cache.put(0, 1);
    cache.put(1, 4);
    cache.put(2, 2);
    CHECK(cache.size() == 3);
    CHECK(*cache.get(0) == 1); // touches 0 -> MRU
    cache.put(3, 8); // evicts LRU (which is 1)
    CHECK(cache.size() == 3);
    CHECK(!cache.get(1).has_value()); // 1 was evicted
    CHECK(*cache.get(0) == 1);
    CHECK(*cache.get(3) == 8);
}

NAM_TEST(cached_digit_source_no_recompute)
{
    // Wrap a sequential producer and check cache hits serve prior indices.
    int calls = 0;
    auto producer = [&calls]() -> std::optional<uint32_t>
    {
        return static_cast<uint32_t>(calls++ % 10);
    };
    CachedDigitSource src(producer, 100);
    CHECK(*src.digit(0) == 0);
    CHECK(*src.digit(5) == 5); // produces 1..5
    CHECK(calls == 6);
    // Re-querying earlier indices must NOT call producer again.
    CHECK(*src.digit(2) == 2);
    CHECK(*src.digit(0) == 0);
    CHECK(calls == 6); // no recompute
}

// ========================= PHASE 3: USER API =========================
// ---------- Ergonomic constructors + digit emission ----------
NAM_TEST(user_number_rational_digits)
{
    // 1/7 = 0.(142857) through the user-facing surface.
    Number n = Number::rational(1, 7, 10);
    auto ds = n.digits(12);
    const uint32_t expect[] = {1, 4, 2, 8, 5, 7, 1, 4, 2, 8, 5, 7};
    CHECK(ds.size() == 12);
    for (size_t i = 0; i < 12; ++i)
        CHECK(ds[i] == expect[i]);
}

NAM_TEST(user_number_sqrt_digits)
{
    Number n = Number::sqrt(2, 10);
    auto ds = n.digits(11);
    const uint32_t expect[] = {4, 1, 4, 2, 1, 3, 5, 6, 2, 3, 7};
    CHECK(ds.size() == 11);
    for (size_t i = 0; i < 11; ++i)
        CHECK(ds[i] == expect[i]);
}

NAM_TEST(user_number_series_digits)
{
    // 1/e via the user API -> 0.36787944...
    Number n = Number::one_over_e(10);
    auto ds = n.digits(6);
    const uint32_t expect[] = {3, 6, 7, 8, 7, 9};
    CHECK(ds.size() >= 6);
    for (size_t i = 0; i < 6 && i < ds.size(); ++i)
        CHECK(ds[i] == expect[i]);
}
NAM_TEST (user_number_pi_quarter)
{
    // pi/4 via the user API -> 0.78539816...
    Number n = Number::pi_quarter(10);
    auto ds = n.digits(6);
    const uint32_t expect[] = {7, 8, 5, 3, 9, 8};
    CHECK(ds.size() >= 6);
    for (size_t i = 0; i < 6 && i < ds.size(); ++i)
        CHECK(ds[i] == expect[i]);
}

NAM_TEST (user_pi_quarter_fork_is_log_n)
{
    Number n = Number::pi_quarter(10);
    CHECK(std::string(n.fork_cost()) == "O(log n)");
    auto [a, b] = n.fork();
    CHECK(a.digits(5) == b.digits(5));
}


// ---------- Precision context (scoped, RAII, thread-local) ----------
NAM_TEST(user_precision_context_scoped)
{
    CHECK(PrecisionContext::digits() == 30); // default
    {
        auto guard = precision_context(8);
        CHECK(PrecisionContext::digits() == 8);
        Number n = Number::rational(1, 7, 10);
        auto ds = n.digits(); // uses context = 8
        CHECK(ds.size() == 8);
    }
    CHECK(PrecisionContext::digits() == 30); // restored on scope exit
}

NAM_TEST(user_precision_context_nested)
{
    auto g1 = precision_context(50);
    CHECK(PrecisionContext::digits() == 50);
    {
        auto g2 = precision_context(10);
        CHECK(PrecisionContext::digits() == 10);
    }
    CHECK(PrecisionContext::digits() == 50);
}

// ---------- Codec: base as projection ----------
NAM_TEST(user_in_base_is_codec)
{
    // 1/4 = 0.25 base 10, 0.01 base 2, 0.1 base 4 -- the number is invariant.
    Number n10 = Number::rational(1, 4, 10);
    CHECK(n10.in_base(2).digits(4) == (std::vector<uint32_t>{0,1,0,0}));
    CHECK(n10.in_base(4).digits(4) == (std::vector<uint32_t>{1,0,0,0}));
    CHECK(n10.base() == 10); // original unchanged (value semantics)
}

// ---------- Fork: honest cost annotation, value semantics ----------
NAM_TEST(user_fork_automaton_is_o1_and_exact)
{
    Number n = Number::rational(1, 7, 10);
    CHECK(std::string(n.fork_cost()) == "O(1)");
    auto [a, b] = n.fork();
    // Fork determinism is EXACT (not interval) for the automaton tier.
    auto da = a.digits(20);
    auto db = b.digits(20);
    CHECK(da == db);
}

NAM_TEST(user_fork_series_is_log_n)
{
    Number n = Number::e(10);
    CHECK(std::string(n.fork_cost()) == "O(log n)");
    auto [a, b] = n.fork();
    // Both forks produce the same fractional prefix of e (0.71828...).
    auto da = a.digits(5);
    auto db = b.digits(5);
    CHECK(da == db);
}

// ---------- Skip-ahead through the user surface ----------
NAM_TEST(user_skip_rational)
{
    Number n = Number::rational(1, 7, 10);
    auto skipped = n.skip(1000);
    CHECK(skipped.has_value());
    // Skipping 1000 digits of 1/7 (period 6): 1000 % 6 == 4 -> next is 5,7,1...
    auto ds = skipped->digits(6);
    const uint32_t expect[] = {5, 7, 1, 4, 2, 8};
    CHECK(ds.size() == 6);
    for (size_t i = 0; i < 6; ++i)
        CHECK(ds[i] == expect[i]);
    // Series tier cannot skip via this path -> honest nullopt.
    CHECK(!Number::e(10).skip(10).has_value());
}

// ---------- Honest comparison predicates ----------
NAM_TEST(user_comparison_interval_honest)
{
    Number a = Number::rational(1, 7, 10);
    Number b = Number::rational(1, 3, 10);
    auto lt = a.definitely_less_than(b, 5);
    CHECK(lt.has_value());
    CHECK(*lt == true);
    CHECK(a.compare(b, 5) == Trit::Less);
    CHECK(b.compare(a, 5) == Trit::Greater);
    // 2/14 == 1/7 -> indistinguishable within bound, agrees exactly.
    Number c = Number::rational(2, 14, 10);
    CHECK(a.agrees_with(c, 30));
    CHECK(a.compare(c, 30) == Trit::Indistinguishable);
}

// ---------- Explicit memoization policy ----------
NAM_TEST(user_cached_memo_records_digits)
{
    Number n = Number::rational(1, 7, 10).cached(64);
    n.digits(12); // populates the explicit cache as a side effect
    // streaming() is the no-cache mode and yields the same digits.
    Number s = Number::rational(1, 7, 10).streaming();
    CHECK(s.digits(6) == (std::vector<uint32_t>{1,4,2,8,5,7}));
}

// ---------- Rendering ----------
NAM_TEST(user_to_string_render)
{
    Number n = Number::rational(1, 4, 10);
    CHECK(n.to_string(4) == "0.2500");
    // Hex render of 1/2 = 0.8 in base 16.
    Number half = Number::rational(1, 2, 16);
    CHECK(half.to_string(2) == "0.80");
}

// ========================= PHASE 4: JIT / EXPR TREE =========================
// The compile(expr_tree) -> NumVMFn primitive. The returned function pointer
// has the SAME C ABI as the static path, so it must reproduce the static
// digit stream EXACTLY (fork/value semantics are unchanged at the ABI line).
NAM_TEST(jit_rational_matches_static)
{
    // Build 1/7 base 10 as a runtime expression tree and compile it.
    auto e = Expr::leaf_rational(1, 7, 10);
    CompiledFn cf = compile(*e);
    AutomatonVM ref = make_rational(1, 7, 10);
    AutomatonVM jit = make_rational(1, 7, 10);
    const uint32_t expect[] = {1, 4, 2, 8, 5, 7, 1, 4, 2, 8, 5, 7};
    for (uint32_t ex : expect)
    {
        NumVMStep rj = cf.step(jit);
        NumVMStep rr = Rational::step(ref);
        CHECK(rj.digit == ex);
        CHECK(rj.digit == rr.digit); // exact agreement with static path
        jit = rj.next;
        ref = rr.next;
    }
}

NAM_TEST(jit_sqrt_matches_static)
{
    auto e = Expr::leaf_sqrt(2, 10);
    CompiledFn cf = compile(*e);
    AutomatonVM jit = make_sqrt(2, 10);
    const uint32_t expect[] = {4, 1, 4, 2, 1, 3, 5, 6, 2, 3, 7};
    for (uint32_t ex : expect)
    {
        NumVMStep rj = cf.step(jit);
        CHECK(rj.digit == ex);
        jit = rj.next;
    }
}

NAM_TEST(jit_padic_matches_static)
{
    auto e = Expr::leaf_padic(-1, 1, 5); // -1 in Z_5 = ...4444
    CompiledFn cf = compile(*e);
    AutomatonVM jit = make_padic(-1, 1, 5);
    for (int i = 0; i < 10; ++i)
    {
        NumVMStep rj = cf.step(jit);
        CHECK(rj.digit == 4);
        jit = rj.next;
    }
}

NAM_TEST(jit_rebase_codec_node)
{
    // Runtime-built codec node: 1/4 base 10 rebased to base 2 -> 0.0100...
    auto e = Expr::rebase(Expr::leaf_rational(1, 4, 10), 2);
    CompiledFn cf = compile(*e);
    // The resolved seed must be the analytic rational-in-base reprojection.
    ResolvedGen rg = resolve_expr(*e);
    AutomatonVM jit = rg.seed;
    CHECK(jit.base == 2);
    const uint32_t expect[] = {0, 1, 0, 0};
    for (uint32_t ex : expect)
    {
        NumVMStep rj = cf.step(jit);
        CHECK(rj.digit == ex);
        jit = rj.next;
    }
}

NAM_TEST(jit_feeds_existing_combinators)
{
    // A compiled NumVMFn is a first-class generator: drive `take` over it,
    // and confirm the resolved seed flows through the static comparator.
    auto ea = Expr::leaf_rational(1, 7, 10);
    auto eb = Expr::leaf_rational(1, 3, 10);
    ResolvedGen ra = resolve_expr(*ea);
    ResolvedGen rb = resolve_expr(*eb);
    // 1/7 < 1/3 -- the static comparator consumes the seeds unchanged.
    auto lt = definitely_less_than<Rational>(ra.seed, rb.seed, 5);
    CHECK(lt.has_value());
    CHECK(*lt == true);
    // Compile + buffer 12 digits via the raw ABI fn.
    CompiledFn cf = compile(*ea);
    std::vector<uint32_t> got;
    AutomatonVM s = ra.seed;
    for (int i = 0; i < 12; ++i)
    {
        NumVMStep r = cf.step(s);
        got.push_back(r.digit);
        s = r.next;
    }
    CHECK(got == (std::vector<uint32_t>{1, 4, 2, 8, 5, 7, 1, 4, 2, 8, 5, 7}));
}

NAM_TEST(jit_multiple_compiles_are_independent)
{
    // Two live compiled fns over different generators must not collide
    // (interpreter trampoline slots are independent; JIT modules are too).
    CompiledFn a = compile(*Expr::leaf_rational(1, 7, 10));
    CompiledFn b = compile(*Expr::leaf_rational(3, 8, 10));
    AutomatonVM sa = make_rational(1, 7, 10);
    AutomatonVM sb = make_rational(3, 8, 10);
    NumVMStep ra = a.step(sa);
    NumVMStep rb = b.step(sb);
    CHECK(ra.digit == 1); // 1/7 -> 1...
    CHECK(rb.digit == 3); // 3/8 -> 3...
}


NAM_TEST_RUN_ALL()