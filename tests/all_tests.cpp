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

using namespace nam;

// ---------- M1: ABI guardrails (compile-time + runtime fork) ----------

NAM_TEST(abi_struct_size) {
    CHECK(sizeof(AutomatonVM) == 40);
    CHECK(std::is_trivially_copyable_v<AutomatonVM>);
    CHECK(std::is_standard_layout_v<AutomatonVM>);
}

NAM_TEST(abi_fork_is_exact) {
    // Fork, then compare digit prefixes -- must be EXACT, not interval.
    AutomatonVM a = make_rational(1, 7, 10);
    AutomatonVM b = num_vm_fork(a);
    for (int i = 0; i < 50; ++i) {
        NumVMStep ra = Rational::step(a);
        NumVMStep rb = Rational::step(b);
        CHECK(ra.digit == rb.digit);
        a = ra.next;
        b = rb.next;
    }
}

// ---------- M2: Rationals ----------

NAM_TEST(rational_one_seventh_base10) {
    // 1/7 = 0.(142857) repeating.
    AutomatonVM v = make_rational(1, 7, 10);
    const uint32_t expect[] = {1,4,2,8,5,7,1,4,2,8,5,7};
    for (uint32_t e : expect) {
        NumVMStep r = Rational::step(v);
        CHECK(r.digit == e);
        v = r.next;
    }
}

NAM_TEST(rational_three_eighths_terminates) {
    // 3/8 = 0.375000...
    AutomatonVM v = make_rational(3, 8, 10);
    const uint32_t expect[] = {3,7,5,0,0,0};
    for (uint32_t e : expect) {
        NumVMStep r = Rational::step(v);
        CHECK(r.digit == e);
        v = r.next;
    }
}

NAM_TEST(rational_period_detection) {
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

NAM_TEST(rational_complexity_is_constant_state) {
    // Logical register count for rationals is 1 (the remainder). The struct
    // size never changes as we emit digits.
    AutomatonVM v = make_rational(1, 7, 10);
    size_t sz = sizeof(v);
    for (int i = 0; i < 100; ++i) v = Rational::step(v).next;
    CHECK(sizeof(v) == sz);   // constant memory
    CHECK(sizeof(v) == 40);
}

// ---------- M3: Quadratic irrationals ----------

NAM_TEST(sqrt2_base10_golden) {
    // sqrt(2) = 1.41421356237...  fractional digits: 4,1,4,2,1,3,5,6,2,3,7
    AutomatonVM v = make_sqrt(2, 10);
    const uint32_t expect[] = {4,1,4,2,1,3,5,6,2,3,7};
    for (uint32_t e : expect) {
        NumVMStep r = Sqrt::step(v);
        CHECK(r.digit == e);
        v = r.next;
    }
}

NAM_TEST(sqrt3_base10_golden) {
    // sqrt(3) = 1.7320508075...  fractional: 7,3,2,0,5,0,8,0,7,5
    AutomatonVM v = make_sqrt(3, 10);
    const uint32_t expect[] = {7,3,2,0,5,0,8,0,7,5};
    for (uint32_t e : expect) {
        NumVMStep r = Sqrt::step(v);
        CHECK(r.digit == e);
        v = r.next;
    }
}

NAM_TEST(sqrt5_base10_golden) {
    // sqrt(5) = 2.2360679...  fractional: 2,3,6,0,6,7,9
    AutomatonVM v = make_sqrt(5, 10);
    const uint32_t expect[] = {2,3,6,0,6,7,9};
    for (uint32_t e : expect) {
        NumVMStep r = Sqrt::step(v);
        CHECK(r.digit == e);
        v = r.next;
    }
}

NAM_TEST(sqrt2_prefix_bitwidth_grows_log_n) {
    // Memory is the complexity metric: the accumulated root prefix register
    // bit-width must grow ~ O(log n) (each digit adds ~log2(base) bits).
    AutomatonVM v = make_sqrt(2, 10);
    int prev = Sqrt::prefix_bitwidth(v);
    std::vector<int> widths;
    for (int i = 0; i < 20; ++i) {
        v = Sqrt::step(v).next;
        int w = Sqrt::prefix_bitwidth(v);
        widths.push_back(w);
        CHECK(w >= prev);        // monotone non-decreasing
        prev = w;
    }
    // After ~n digits in base 10, width is about n*log2(10) ~ 3.32*n.
    // Check it stays in a linear-in-n (=> O(log n)-per-digit) envelope and
    // is well below n^2 nonsense.
    CHECK(widths.back() <= 20 * 4 + 8);
    CHECK(widths.back() >= 20 * 3);
}

// ---------- M4: Codec layer ----------

NAM_TEST(rational_base_change_is_codec) {
    // 1/4 = 0.25 in base 10, = 0.1 in base 4, = 0.01 in base 2.
    AutomatonVM b10 = make_rational(1, 4, 10);
    {
        const uint32_t e10[] = {2,5,0,0};
        AutomatonVM v = b10;
        for (uint32_t e : e10) { auto r = Rational::step(v); CHECK(r.digit==e); v=r.next; }
    }
    AutomatonVM b2 = rational_in_base(b10, 2);
    {
        const uint32_t e2[] = {0,1,0,0};
        AutomatonVM v = b2;
        for (uint32_t e : e2) { auto r = Rational::step(v); CHECK(r.digit==e); v=r.next; }
    }
    AutomatonVM b4 = rational_in_base(b10, 4);
    {
        const uint32_t e4[] = {1,0,0,0};
        AutomatonVM v = b4;
        for (uint32_t e : e4) { auto r = Rational::step(v); CHECK(r.digit==e); v=r.next; }
    }
}

NAM_TEST(codec_roundtrip_reproject) {
    // Reproject 1/3 base 10 -> base 7 -> back, agree to N digits.
    AutomatonVM src = make_rational(1, 3, 10);
    // Read 12 base-10 digits, emit base-7 digits.
    auto b7 = reproject_digits<Rational>(src, 7, 12, 6);
    // 1/3 in base 7 = 0.222222... (since 1/3 = 2/7 + 2/49 + ...).
    for (size_t i = 0; i < b7.size(); ++i) CHECK(b7[i] == 2);
}

// ---------- M5: p-adics ----------

NAM_TEST(padic_minus_one_in_z5_is_all_fours) {
    // -1 in Z_5 = ...444444 (each digit 4). a=-1, b=1, p=5.
    AutomatonVM v = make_padic(-1, 1, 5);
    for (int i = 0; i < 10; ++i) {
        NumVMStep r = PAdic::step(v);
        CHECK(r.digit == 4);
        v = r.next;
    }
}

NAM_TEST(padic_one_third_in_z5) {
    // 1/3 in Z_5. 3^{-1} mod 5 = 2, so first digit 2. Known expansion of
    // 1/3 in Z_5 is ...131313 2 ... let's just check it is periodic and the
    // partial sum reconstructs 1/3 mod 5^k.
    AutomatonVM v = make_padic(1, 3, 5);
    // Reconstruct value mod 5^k from digits and check == 1/3 mod 5^k,
    // i.e. (3 * value) % 5^k == 1.
    long long val = 0, pk = 1;
    for (int k = 0; k < 8; ++k) {
        NumVMStep r = PAdic::step(v);
        val += (long long)r.digit * pk;
        pk *= 5;
        CHECK(((3 * val) % pk + pk) % pk == 1 % pk);
        v = r.next;
    }
}

NAM_TEST(padic_period_is_finite) {
    // 1/3 in Z_5 is purely periodic; period divides the multiplicative
    // order. Just assert a finite period is found.
    auto [pre, per] = padic_period(make_padic(1, 3, 5));
    CHECK(per > 0);
    (void)pre;
}

NAM_TEST(padic_valuation_extractor) {
    // v_5(50) = 2 (50 = 2 * 5^2). v_5(7) = 0. v_5(125) = 3.
    CHECK(p_valuation(50, 5) == 2);
    CHECK(p_valuation(7, 5) == 0);
    CHECK(p_valuation(125, 5) == 3);
}

// ---------- M6: comparison + p-adic metric ----------

NAM_TEST(compare_interval_honest) {
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

NAM_TEST(padic_metric_product_automaton) {
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

NAM_TEST(padic_metric_equal_is_pending) {
    AutomatonVM x = make_padic(1, 3, 5);
    AutomatonVM y = make_padic(1, 3, 5);
    auto v = padic_valuation_of_difference<PAdic>(x, y, 10);
    CHECK(!v.has_value()); // agree on whole prefix -> honest pending
}

// ---------- M7: skip-ahead ----------

NAM_TEST(skip_rational_matches_naive) {
    // Jump 1000 digits of 1/7 base 10 and compare to naive stepping.
    AutomatonVM v = make_rational(1, 7, 10);
    AutomatonVM fast = skip_rational(1000, v);
    AutomatonVM slow = skip_naive<Rational>(1000, v);
    // Compare next 12 digits -- must be EXACT.
    for (int i = 0; i < 12; ++i) {
        NumVMStep rf = Rational::step(fast);
        NumVMStep rs = Rational::step(slow);
        CHECK(rf.digit == rs.digit);
        fast = rf.next;
        slow = rs.next;
    }
}

NAM_TEST(skip_rational_terminating) {
    // 3/8 terminates; skipping past the significant digits yields zeros.
    AutomatonVM v = make_rational(3, 8, 10);
    AutomatonVM fast = skip_rational(100, v);
    for (int i = 0; i < 5; ++i) {
        NumVMStep r = Rational::step(fast);
        CHECK(r.digit == 0);
        fast = r.next;
    }
}

NAM_TEST(mat_pow_modexp_kernel) {
    // The general fast-forward kernel: Fibonacci via [[1,1],[1,0]]^n.
    // F(10) = 55. Matrix power top-left after ^10 with no modulus reduction
    // (large modulus) equals F(11)=89; check the standard identity
    // [[F(n+1),F(n)],[F(n),F(n-1)]].
    Mat2 fib{1,1,1,0};
    Mat2 p = mat_pow(fib, 10, (uint64_t)1e18);
    CHECK(p.b == 55); // F(10)
    CHECK(p.a == 89); // F(11)
    CHECK(p.d == 34); // F(9)
}

NAM_TEST_RUN_ALL()