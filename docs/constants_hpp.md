# `nam/constants.hpp` — A Standard Repertoire of Series-Tier Constants

## Overview

This header implements **Phase 2** of the NAM ("Numbers As Machines")
project: a curated library of fundamental mathematical constants, each
defined not by a stored value but by a *convergent series* paired with a
compiled **convergence proof** in the form of a tail-bound oracle.

Per `THEORY.md`:

> *"The library must ship a standard repertoire of these oracles (for pi,
> e, log, ...)."*

Each constant is represented as a `SeriesSpec` — a pair of callbacks:

- **`advance(n, num, den)`** — extends an exact rational partial sum,
  accumulating `num/den` over a *running common denominator*.
- **`tail_bound(n, den)`** — returns a certified upper bound on the
  truncation error of the partial sum after `n` terms.

Together these allow the digit extractor (`refine.hpp`) to emit verified
digits of an irrational constant on demand, to arbitrary precision.

---

## Theoretical Background

### Exact-rational partial sums

Rather than carry floating-point approximations (which accumulate
rounding error and forfeit certifiability), NAM accumulates each constant
as a sequence of **exact rationals** `num/den`. This is the classical
technique of *unbounded-precision rational arithmetic*, used in computer
algebra systems and in arbitrary-precision constant libraries such as
the work of Jonathan and Peter Borwein, and Richard Brent's MP package
(Brent, *"A Fortran multiple-precision arithmetic package"*, ACM TOMS,
1978).

### The exact-rescale invariant

A central design constraint, relied upon by `refine.hpp`, is that the
denominator at step `n+1` must be an **integer multiple** of the
denominator at step `n`. This guarantees that previously-extracted digits
remain valid as more terms are added — i.e. the digit stream is *monotone*
and *stable* under refinement. Each spec therefore maintains a denominator
that grows by a known integer factor:

| Constant   | Denominator after `n` terms |
|------------|-----------------------------|
| `e`, `1/e` | `n!`                        |
| `ln2`      | `n! · 2ⁿ`                   |

Because `(n+1)!` is divisible by `n!`, and `(n+1)!·2^(n+1)` is divisible
by `n!·2ⁿ`, the invariant holds by construction.

### Tail bounds as certificates

Each oracle returns an integer `err` such that

```
  | value − num/den |  ≤  err / den.
```

This is the constant's **convergence proof**, evaluated lazily. The bounds
used here are standard textbook results:

- **Exponential series** `e = Σ_{k≥0} 1/k!`. The Taylor remainder of the
  exponential function satisfies, for `n ≥ 1`,
  `Σ_{k≥n} 1/k! ≤ 2/n!` (a geometric-domination argument, since
  successive terms decay faster than a ratio-½ geometric series once
  `k ≥ 1`). Hence `err = 2`. See Rudin, *Principles of Mathematical
  Analysis*, §3 (series), or any treatment of the exponential function.

- **Alternating exponential** `1/e = Σ_{k≥0} (−1)^k/k!`. By the
  **alternating series (Leibniz) test**, the truncation error is bounded
  in magnitude by the first omitted term, `1/n!`. Hence `err = 1`.

- **Mercator / logarithm series for ln 2** `ln 2 = Σ_{k≥1} 1/(k·2^k)`.
  This is the value at `x = 1/2` of the series
  `−ln(1−x) = Σ_{k≥1} x^k/k` (Mercator, 1668; see also Nicholas
  Mercator's *Logarithmotechnia*). The tail satisfies
  `Σ_{k>n} 1/(k·2^k) ≤ Σ_{k>n} 1/2^k = 1/2^n`, giving
  `err = den/2^n = n!`.

- **Catalan's constant** `G = Σ_{k≥0} (−1)^k/(2k+1)^2`. This is a slowly
  (linearly) convergent alternating series whose truncation error, by the
  alternating series (Leibniz) test, is bounded by the first omitted term:
  `|tail| ≤ 1/(2n+1)^2`. Over the running denominator (the product of the
  squared odd numbers) this gives `err = den/(2n+1)^2`. Because the series
  converges slowly, many terms are needed for high precision — a faithful
  demonstration that the tail-bound oracle, not convergence speed, is what
  guarantees honest digit commitment.

---

## Provided Constants

| Builder             | Value      | Series                    |
|---------------------|------------|---------------------------|
| `e_spec()`          | `2.71828…` | `Σ_{k≥0} 1/k!`            |
| `ln2_spec()`        | `0.69314…` | `Σ_{k≥1} 1/(k·2^k)`       |
| `one_over_e_spec()` | `0.36787…` | `Σ_{k≥0} (−1)^k/k!`       |
| `pi_quarter_spec()` | `0.78539…` | `arctan(1/2)+arctan(1/3)` |
| `catalan_spec()`    | `0.91596…` | `Σ_{k≥0} (−1)^k/(2k+1)^2` |

Convenience wrappers materialise a `SeriesVM` in a chosen radix:

```cpp
nam::SeriesVM e   = nam::make_e();        // base 10 by default
nam::SeriesVM l2  = nam::make_ln2(2);     // binary digits of ln 2
nam::SeriesVM ie  = nam::make_one_over_e();
nam::SeriesVM piq = nam::make_pi_quarter(); // 0.78539... = pi/4
```

> **Note.** `e` itself has integer part `2`; the fractional-part extractor
> consumes `0.71828…`. `ln2` and `1/e` are already in `(0,1)`.

---

## API Reference

### `e_spec() → shared_ptr<const SeriesSpec>`

Builds the spec for Euler's number `e`.

- **State invariant on entry to `advance(n, …)`**: `den = n!` and
  `num = n! · Σ_{k<n} 1/k!` (with `den = 1`, `num = 0` at `n = 0`).
- **`advance`**: rescales `num`, `den` by `n` (moving to denominator
  `n!`) and adds the unit numerator for the `k = n` term.
- **`tail_bound`**: returns `2` (or `3` before any terms are summed).

### `ln2_spec() → shared_ptr<const SeriesSpec>`

Builds the spec for the natural logarithm of two.

- **State invariant**: `den = n! · 2ⁿ` on entry.
- **`advance`**: for term `k = n+1`, the new denominator is
  `den · k · 2 = k! · 2^k`; the term numerator is `(k−1)!`, obtained by
  exact division `newden / (k · 2^k)`.
- **`tail_bound`**: returns `n!`, recovered as `den / 2ⁿ`.

### `one_over_e_spec() → shared_ptr<const SeriesSpec>`

Builds the spec for `1/e` via the alternating exponential series.

- **State invariant**: identical to `e_spec()` (`den = n!`), with signs
  alternating in `advance`.
- **`tail_bound`**: returns `1` (Leibniz bound), or `2` at `n = 0`.

---

## Implementation Notes & Trade-offs

- The `ln2` denominator `n!·2ⁿ` is deliberately *coarser than necessary*
  (the true least common denominator of the first `n` terms is smaller).
  This sacrifices compactness to **preserve the exact-rescale invariant**
  cheaply, which is the property `refine.hpp` depends upon. A future phase
  may switch to an LCM-based accumulator if memory growth becomes a
  concern.

- The `ln2` term numerator `(k−1)!` is recovered by an exact `divmod`
  rather than maintained incrementally. The remainder `r` is provably
  zero by the divisibility argument above; it is discarded.

- Tail bounds are intentionally **conservative constants** where a tighter
  asymptotic bound would complicate the code without affecting
  correctness — a valid bound is all the digit extractor requires, since
  it only needs to know when adjacent partial-sum intervals separate a
  digit boundary.

---

## Correctness Caveats

The denominator-state comments inside `e_spec`'s `tail_bound` note an
ambiguity between `(n−1)!` and `n!` interpretations of `den`. The returned
bound (`2`) is safe under either reading because `2/n!` dominates the
exponential tail for all `n ≥ 1`. Should the accumulator's invariant be
tightened, these bounds should be re-derived accordingly.

---

## References

1. W. Rudin, *Principles of Mathematical Analysis*, 3rd ed.,
   McGraw-Hill, 1976 — series convergence, the exponential function, and
   the alternating series test.
2. R. P. Brent, *"A Fortran Multiple-Precision Arithmetic Package"*,
   ACM Transactions on Mathematical Software 4(1), 1978 — foundational
   arbitrary-precision constant computation.
3. J. M. Borwein & P. B. Borwein, *Pi and the AGM: A Study in Analytic
   Number Theory and Computational Complexity*, Wiley, 1987 — series-based
   constant evaluation with rigorous error control.
4. N. Mercator, *Logarithmotechnia*, 1668 — the series for `ln(1+x)`
   underlying the `ln2` accumulator.
5. Project document `THEORY.md` — the tail-bound oracle abstraction and
   the standard-repertoire requirement.

## See Also

- [`nam/series.hpp`](series_hpp.md) — `SeriesSpec`, `SeriesVM`, and
  `make_series`.
- [`nam/refine.hpp`](refine_hpp.md) — the fractional digit extractor that
  consumes these specs.
- [`nam/big_int.hpp`](big_int_hpp.md) — `BigInt`, `big_pow`, and
  `BigInt::divmod`.