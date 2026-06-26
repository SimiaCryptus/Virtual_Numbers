# `metric.hpp` — The p-adic Metric as a Product Automaton

**Module:** M6 (Section 3.7)
**Header:** `include/nam/metric.hpp`
**Namespace:** `nam`

---

## Overview

This module computes the **p-adic distance** between two p-adic generators by
running them as a *product automaton* — stepping both machines in lockstep and
detecting the first digit at which their expansions diverge. When both inputs
are periodic, the comparison itself forms a finite periodic machine, so the
computation is exact.

---

## Mathematical Background

### The p-adic numbers

Fix a prime `p`. Every nonzero p-adic integer (and, more generally, every
element of the field of p-adic numbers `ℚ_p`) admits a unique expansion

```
x = Σ_{i ≥ k} a_i · p^i,    a_i ∈ {0, 1, …, p-1},  a_k ≠ 0.
```

The integer `k` is the **p-adic valuation** `v_p(x)`. By convention
`v_p(0) = +∞`. The valuation is a discrete valuation in the sense of
Krull (1932) [4], satisfying:

- `v_p(xy) = v_p(x) + v_p(y)`,
- `v_p(x + y) ≥ min(v_p(x), v_p(y))`, with equality when the valuations differ.

This second property is the **non-archimedean (ultrametric) inequality**, the
defining feature that distinguishes p-adic analysis from the familiar real
setting (Koblitz, *p-adic Numbers, p-adic Analysis, and Zeta-Functions* [1];
Gouvêa, *p-adic Numbers: An Introduction* [2]).

### The p-adic absolute value and metric

The valuation induces the **p-adic absolute value**

```
|x|_p = p^{-v_p(x)},    |0|_p = 0,
```

and the associated metric

```
d_p(x, y) = |x - y|_p = p^{-v_p(x - y)}.
```

This metric satisfies the **strong triangle inequality**

```
d_p(x, z) ≤ max( d_p(x, y), d_p(y, z) ),
```

making `(ℚ_p, d_p)` an **ultrametric space**. Ostrowski's theorem (1916) [3]
establishes that, up to equivalence, the only nontrivial absolute values on
`ℚ` are the usual archimedean one and the p-adic ones — so this metric family
is, in a precise sense, canonical.

### Reading the distance off the digit expansions

The crucial computational fact exploited by this module:

> `v_p(x - y)` equals the index of the **first digit** at which the
> expansions of `x` and `y` (read least-significant-digit upward) differ.

If `x` and `y` agree on digits `a_0, a_1, …, a_{i-1}` but differ at digit `i`,
then `x - y` is divisible by `p^i` but not `p^{i+1}`, so `v_p(x - y) = i` and
`d_p(x, y) = p^{-i}`. Thus the metric requires **no subtraction and no
arithmetic** — only a lockstep comparison of digit streams.

---

## The Product-Automaton Perspective

Each input is a p-adic generator: a finite state machine emitting digits LSB
upward (see `generator.hpp`, M-series). Comparing two such generators is the
classical construction of a **product (synchronous) automaton** over the
Cartesian product of the two state spaces (Hopcroft & Ullman [5]).

- We advance both machines together: `(x, y) ↦ (step(x), step(y))`.
- The first index where emitted digits disagree is the valuation `v_p(x - y)`.
- If both generators are **eventually periodic**, the product is eventually
  periodic, so the search terminates within the bounded prefix; the result is
  exact rather than approximate.

This mirrors the ultrametric structure directly in computation: the metric
is decided by a *finite* observation of the digit stream.

---

## API Reference

### `padic_valuation_of_difference`

```cpp
template <Generator G>
std::optional<int> padic_valuation_of_difference(AutomatonVM x, AutomatonVM y,
                                                 int max_digits);
```

Steps both generators in lockstep for up to `max_digits` digits and returns
the LSB-up index `i` of the first differing digit — i.e. `v_p(x - y)`.

- **Returns** `i` (the valuation) if a disagreement is found.
- **Returns** `std::nullopt` if the two expansions agree on the entire scanned
  prefix. This is an honest "pending" signal: the true distance is strictly
  smaller than `p^{-max_digits}`, but we cannot resolve it without examining
  more digits. (Note this never returns `+∞` for genuinely equal inputs —
  equality is only ever observed *up to the prefix length*.)

| Parameter    | Meaning                                        |
|--------------|------------------------------------------------|
| `x`, `y`     | The two p-adic generator VM states to compare. |
| `max_digits` | Bound on how many digits to scan.              |

### `padic_distance`

```cpp
template <Generator G>
std::optional<double> padic_distance(AutomatonVM x, AutomatonVM y,
                                     int max_digits);
```

Computes `d_p(x, y) = p^{-v_p(x - y)}` as a `double`, using
`padic_valuation_of_difference` to obtain the valuation and `x.base` as the
prime `p`.

- **Returns** `p^{-v}` when the valuation is found.
- **Returns** `std::nullopt` when the prefix fully agrees within `max_digits`
  (the distance is below the resolvable bound).

---

## Design Notes

- **Exactness under periodicity.** Because periodic generators yield a
  periodic product automaton, the valuation — when it exists below the period
  bound — is mathematically exact, not a floating-point approximation. The
  final `std::pow(p, -v)` conversion in `padic_distance` is the *only* place
  floating point enters; downstream code needing exact ultrametric comparisons
  should prefer `padic_valuation_of_difference` and compare valuations
  (larger valuation ⇔ smaller distance) directly.

- **Bounded, honest reporting.** The `std::optional` return type encodes the
  intrinsic limitation of observing a stream through a finite window: absence
  of an answer means "indistinguishable within budget," never a silent
  fabrication of equality.

- **No arithmetic on the inputs.** The ultrametric is decided purely by digit
  comparison, faithful to the valuation-theoretic definition above.

---

## References

1. N. Koblitz, *p-adic Numbers, p-adic Analysis, and Zeta-Functions*,
   2nd ed., Graduate Texts in Mathematics 58, Springer, 1984.
2. F. Q. Gouvêa, *p-adic Numbers: An Introduction*, 3rd ed., Springer, 2020.
3. A. Ostrowski, "Über einige Lösungen der Funktionalgleichung
   φ(x)·φ(y) = φ(xy)," *Acta Mathematica* 41 (1916), 271–284.
   (Ostrowski's theorem on absolute values of ℚ.)
4. W. Krull, "Allgemeine Bewertungstheorie," *J. Reine Angew. Math.* 167
   (1932), 160–196. (General valuation theory.)
5. J. E. Hopcroft and J. D. Ullman, *Introduction to Automata Theory,
   Languages, and Computation*, Addison-Wesley, 1979.
   (Product/synchronous automaton constructions.)

## See Also

- `generator.hpp` — the p-adic generator (Generator concept) producing
  LSB-up digit streams.
- `nam/abi.h` — `AutomatonVM`, `NumVMStep`, and the digit/base ABI.