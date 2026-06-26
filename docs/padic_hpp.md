# p-adic Numbers: Local Digit Commitment (M5)

## Overview

This module (`include/nam/padic.hpp`) implements a digit generator for
**p-adic numbers**, realizing milestone M5 of the project (Section 3.4).
The p-adic numbers furnish a clean example of *locality*: each output digit
depends only on the current local state, and the state recurrence involves
an *exact* integer division. This is the "easy win" of the framework -- the
digit commitment is purely local and requires no carries or look-ahead.

---

## Mathematical Background

### The field of p-adic numbers

For a fixed prime `p`, the field of p-adic numbers `Q_p` is the completion of
the rationals `Q` with respect to the **p-adic absolute value**

```
    |x|_p = p^{-v_p(x)},
```

where `v_p(x)` is the *p-adic valuation* (the exponent of `p` in the prime
factorization of `x`). This construction was introduced by Kurt Hensel in
1897 [Hensel 1908] and is now foundational in number theory, algebraic
geometry, and the local-global principle of Hasse and Minkowski.

Every p-adic number admits a canonical **Hensel expansion**

```
    x = sum_{k >= v} d_k p^k ,    d_k in {0, 1, ..., p-1},
```

indexed from the least-significant digit *upward*. Unlike the decimal
expansion of a real number, this series converges in the p-adic metric and
the expansion is *unique*. The ring of p-adic integers `Z_p` corresponds to
those `x` with `v_p(x) >= 0`, i.e. expansions with no negative powers of `p`.

### Eventual periodicity of rationals

A key classical fact (the p-adic analogue of the eventual periodicity of
repeating decimals) states:

> A p-adic number `x in Q_p` lies in `Q` if and only if its Hensel expansion
> is **ultimately periodic** [Mahler 1973; Gouvea 1997, Ch. 3].

This is precisely what makes the generator amenable to *skip-ahead*: a
rational `a/b` with `gcd(b, p) = 1` has a finite reachable state space, so the
digit stream enters a periodic orbit. Once the orbit is detected, arbitrary
digit positions can be computed in `O(1)` amortized time rather than by
marching the recurrence forward. This mirrors the cycle-detection ideas of
Floyd and Brent for functional iteration [Brent 1980].

---

## The Digit Recurrence

Given `x = a/b in Z_p` with `b` invertible modulo `p`, the next digit and
reduced numerator are computed by:

```
    d  = (a * b^{-1}) mod p              // the local digit in {0,...,p-1}
    a' = (a - d * b) / p                 // EXACT integer division
```

The exactness of the division is the locality property: by construction
`a - d*b` is divisible by `p`, so no remainder or carry leaks into subsequent
positions. The denominator `b` is invariant throughout the iteration.

The modular inverse `b^{-1} mod p` is obtained via the **extended Euclidean
algorithm** [Knuth 1997, Vol. 2, Sec. 4.5.2], which simultaneously solves
`b * s + p * t = gcd(b, p) = 1`, yielding `s = b^{-1} mod p`. If
`gcd(b, p) != 1` the inverse does not exist and the routine returns `0` as a
sentinel.

---

## AutomatonVM Layout

The generator stores its configuration in the shared `AutomatonVM` structure:

| Field      | Meaning                                                |
|------------|--------------------------------------------------------|
| `base`     | the prime `p` (also the output codec radix)            |
| `phase`    | digit index, counted LSB-up (`0` = least significant)  |
| `state[0]` | current numerator `a` (signed, stored in `int64` bits) |
| `state[1]` | denominator `b`                                        |

Numerators may become negative during reduction, so `state[0]` is interpreted
as a two's-complement `int64_t`. A floor-division helper (`detail::floordiv`)
is provided for situations requiring rounding toward negative infinity,
consistent with the conventions used elsewhere in the codebase.

---

## API Reference

### `struct PAdic`

Satisfies the `Generator` concept (enforced via `static_assert`). Its single
static method

```cpp
    static NumVMStep step(AutomatonVM s);
```

emits one digit and advances the state by one position according to the
recurrence above.

### `make_padic(int64_t a, int64_t b, uint32_t p)`

Constructs an `AutomatonVM` representing the p-adic expansion of `a/b` in
`Z_p`. **Precondition:** `gcd(b, p) == 1` (so that `b` is invertible and the
expansion is well-defined as a p-adic integer).

### `p_valuation(int64_t n, int64_t p)` — Valuation Extractor

Computes the p-adic valuation `v_p(n)`: the largest `k` with `p^k | n`. For a
reduced fraction `a/b` with `gcd(b, p) = 1` this equals `v_p(a)`. By
convention `v_p(0) = +infinity`; the function returns `-1` as a finite
sentinel for that case.

### `padic_period(AutomatonVM vm)`

Detects the eventual period of the digit stream by recording the sequence of
numerators `a` and finding the first repetition. Returns a pair
`(mu, lambda)` where:

- `mu` is the length of the *pre-period* (the index of first repeated state),
- `lambda` is the *period* length of the repeating cycle.

Because `b` is fixed and `a` is reduced at each step, the orbit is finite when
`|a|` is bounded by `|b|`, guaranteeing termination. A safety cap of
`1,000,000` iterations guards against pathological inputs (returning
`(idx, 0)` if exceeded).

---

## Worked Example

Consider `x = 1/3` in `Z_5` (`p = 5`, `gcd(3, 5) = 1`):

```
    3^{-1} mod 5 = 2     (since 3*2 = 6 ≡ 1 mod 5)
    d_0 = 1*2 mod 5 = 2,  a' = (1 - 2*3)/5 = -5/5 = -1
    d_1 = (-1)*2 mod 5 = 3, a' = (-1 - 3*3)/5 = -10/5 = -2
    ...
```

yielding the ultimately periodic expansion `1/3 = ...1313132_5`, consistent
with the classical theory of repeating Hensel codes.

---

## References

- K. Hensel, *Theorie der algebraischen Zahlen*, Teubner, 1908.
- F. Q. Gouvea, *p-adic Numbers: An Introduction*, 2nd ed., Springer, 1997.
- K. Mahler, *Introduction to p-adic Numbers and Their Functions*,
  Cambridge University Press, 1973.
- D. E. Knuth, *The Art of Computer Programming*, Vol. 2: *Seminumerical
  Algorithms*, 3rd ed., Addison-Wesley, 1997 (Sec. 4.5.2, extended Euclid).
- R. P. Brent, "An improved Monte Carlo factorization algorithm",
  *BIT* 20 (1980), 176–184 (cycle detection).
- N. Koblitz, *p-adic Numbers, p-adic Analysis, and Zeta-Functions*,
  2nd ed., Springer, 1984.