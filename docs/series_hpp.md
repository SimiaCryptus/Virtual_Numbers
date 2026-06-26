# `series.hpp` — The SERIES Tier

## Overview

`series.hpp` implements the second tier of the **Forkable Nano-VM**
two-tier ABI described in `THEORY.md`. Where the *automaton* tier
models periodic / eventually-periodic constants with **bounded**,
copy-on-write state, the **series** tier models classical
transcendentals — `e`, `π`, `log 2`, `ζ(3)`, Catalan's constant, and
friends — whose state **grows** with the number of terms accumulated.

The central design decision is honesty about cost: because the state
of a series VM is a pair of arbitrary-precision integers that grow as
more terms are summed, `fork()` is an **explicit deep copy**, not
copy-on-write. This is documented and instrumented so the cost of
forking is never hidden from the caller.

---

## Theoretical Background

### 1. Convergent series as a representation of reals

A real constant `x` is represented by a sequence of partial sums
`S_0, S_1, S_2, …` together with a *tail bound* (remainder estimate)
guaranteeing `|x − S_n| ≤ R_n` with `R_n → 0`. This is the classical
**Cauchy-sequence-with-modulus** representation of a computable real,
formalised in:

- E. Bishop & D. Bridges, *Constructive Analysis* (1985) — a real
number *is* a Cauchy sequence equipped with an explicit modulus of
convergence.
- A. Turing, "On Computable Numbers, with an Application to the
Entscheidungsproblem" (1936) — the foundational notion of a number
computable to arbitrary precision by a finite process.
- K. Weihrauch, *Computable Analysis* (2000) — the Type-2 Theory of
Effectivity (TTE), where a real is given by a name producing
arbitrarily good rational approximations.

In this file the partial sum `S_n` is held as an **exact rational**
`num / den` (both `BigInt`), and the modulus of convergence is the
`tail_bound` oracle: `|x − num/den| ≤ err / den`.

### 2. The Tail-Bound Oracle as a compiled convergence proof

A series is only usable as a representation of a real number if we can
*certify* how far the partial sum can still move. The `tail_bound`
function is exactly this certificate. It must be **monotone
non-increasing** in the term index — a property that corresponds to a
proof that the remainder is bounded by a decreasing majorant
(e.g. comparison with a geometric series, the Leibniz alternating-
series remainder bound, or a Stirling-type ratio test).

This mirrors the use of *recursive moduli of convergence* in
constructive mathematics and the "regular number" formulation used by
H. Cohen, *A Course in Computational Algebraic Number Theory* (1993)
and by interval-arithmetic libraries such as MPFI and Arb
(F. Johansson, "Arb: Efficient Arbitrary-Precision Midpoint-Radius
Interval Arithmetic", *IEEE Trans. Computers*, 2017).

### 3. Interval refinement & honest digit commitment

Digits are emitted only by **interval refinement**: a leading digit is
committed only when the enclosing interval
`[num/den, (num + err)/den]` is narrow enough that the digit is
unambiguous. This is the *midpoint–radius (ball) arithmetic*
discipline of Arb and the classical concern of guaranteeing that no
digit is ever falsely committed — a subtlety known since the
"Table-Maker's Dilemma" (cf. W. Kahan) and central to correctly-
rounded transcendental evaluation. We never output a digit we might
later have to retract.

### 4. State that grows: memory *is* the complexity metric

Unlike the automaton tier, the accumulators here grow without bound
as precision increases. The natural complexity measure is therefore
the **live accumulator bit-width**, exposed via
`accumulator_bitwidth()`. This reflects the standard result that the
cost of computing `n` digits of a typical transcendental via series
summation is governed by the size of the accumulated numerators and
denominators — the analysis behind **binary splitting** and the
bit-complexity bounds of:

- R. P. Brent & P. Zimmermann, *Modern Computer Arithmetic* (2010),
Ch. 4 (Newton iteration, binary splitting, the cost of `e`, `π`).
- E. A. Karatsuba, "Fast evaluation of transcendental functions"
(1991).
- The Chudnovsky brothers' series for `π` (1988), the prototypical
rapidly-convergent hypergeometric series.

Because the metric is memory, the implementation simply reports the
bit-width of `num` and `den` rather than hiding it behind an opaque
step counter.

### 5. Fork = explicit deep copy

In the Forkable Nano-VM model, *forking* a computation means cloning
its state to explore alternative continuations (refinement,
speculative digit emission, parallel evaluation). For the automaton
tier this is cheap (bounded, COW). For the series tier the state is
`O(log n)` integers in the depth, so `fork()` performs a genuine deep
copy of `num` and `den` while **aliasing** the immutable `SeriesSpec`.
This is the explicit, instrumented cost contract demanded by
`THEORY.md`'s section *"The Forkable Nano-VM: A Two-Tier ABI"*.

---

## ABI / Data Structures

### `SeriesSpec` — immutable, shareable

Describes *how* to advance and *how* to bound a series. It is immutable
and may be freely aliased / shared across VMs (held by
`shared_ptr<const SeriesSpec>`).

| Member        | Type                                                       | Role |
|---------------|------------------------------------------------------------|------|
| `advance`     | `void(uint64_t n, BigInt& num, BigInt& den)`               | Advance the partial sum from `S_n` to `S_{n+1}`, keeping `den` as a running common denominator. |
| `tail_bound`  | `BigInt(uint64_t n, const BigInt& den)`                    | Return `err` with `|x − num/den| ≤ err/den`. **Must be monotone non-increasing** in `n`. |
| `name`        | `const char*`                                              | Diagnostic / golden-test label. |

The split between `advance` (mechanism) and `tail_bound` (certificate)
deliberately separates the *computation* from its *correctness proof*,
echoing the modulus-of-convergence discipline of constructive analysis.

### `SeriesVM` — mutable accumulators

| Member    | Type                              | Role |
|-----------|-----------------------------------|------|
| `base`    | `uint32_t`                        | Radix for digit emission (default 10). |
| `index`   | `uint64_t`                        | Number of terms accumulated so far. |
| `spec`    | `shared_ptr<const SeriesSpec>`    | Aliased immutable behaviour. |
| `num`     | `BigInt`                          | Partial-sum numerator (mutable). |
| `den`     | `BigInt`                          | Common denominator (mutable). |

#### Methods

- **`SeriesVM fork() const`**
Explicit **deep copy** of `num` and `den`; aliases `spec`. Cost is
`O(accumulator bit-width)` — *not* copy-on-write. This is the
documented, honest forking contract.

- **`void step_term()`**
Invokes `spec->advance(index, num, den)` then increments `index`.
Accumulates exactly one more series term.

- **`BigInt tail() const`**
Returns the current tail-bound numerator `err` from
`spec->tail_bound(index, den)`. The certified error is `err/den`.

- **`int accumulator_bitwidth() const`**
The complexity metric for this tier:
`num.bit_width() + den.bit_width()`. Memory *is* the metric.

### `make_series(spec, base)`

Factory constructing a `SeriesVM` with the empty partial sum
`S_0 = 0/1`:

```cpp
auto vm = nam::make_series(spec, /*base=*/10);
// vm.num == 0, vm.den == 1, vm.index == 0
```

---

## Design Invariants

1. **No global mutable state.** Memoisation is opt-in via the explicit
`memo.hpp` wrapper; the core VM is referentially transparent given a
spec.
2. **Immutable spec, mutable accumulators.** Only `num`, `den`, and
`index` change; `spec` is shared and never mutated.
3. **Monotone tail bound.** `tail_bound` must be non-increasing in
`index`; this is the compiled convergence proof.
4. **Honest commitment.** Digits are emitted only through interval
refinement; a digit is never committed while ambiguous.
5. **Explicit fork cost.** `fork()` is a deep copy and is instrumented
via `accumulator_bitwidth()`.

---

## Usage Sketch

```cpp
 #include "nam/series.hpp"

 // A spec for, e.g., e = sum 1/k!  (advance + tail_bound elided)
 auto spec = std::make_shared<const nam::SeriesSpec>(make_e_spec());

 nam::SeriesVM vm = nam::make_series(spec, 10);
 while (vm.tail() /* too large for desired precision */) {
     vm.step_term();
 }

 // Fork to explore a speculative continuation (deep copy!):
 nam::SeriesVM branch = vm.fork();
 branch.step_term();
```

---

## References

- A. M. Turing, "On Computable Numbers…", *Proc. London Math. Soc.*,
1936.
- E. Bishop & D. Bridges, *Constructive Analysis*, Springer, 1985.
- K. Weihrauch, *Computable Analysis: An Introduction*, Springer, 2000.
- R. P. Brent & P. Zimmermann, *Modern Computer Arithmetic*, CUP, 2010.
- F. Johansson, "Arb: Efficient Arbitrary-Precision Midpoint-Radius
Interval Arithmetic", *IEEE Trans. Computers* 66(8), 2017.
- H. Cohen, *A Course in Computational Algebraic Number Theory*,
Springer, 1993.
- D. V. & G. V. Chudnovsky, "Approximations and complex multiplication
according to Ramanujan", 1988.
- Project `THEORY.md`, sections *"The Forkable Nano-VM: A Two-Tier
ABI"*, *"The Odd Primitives"*, and *"Tail Bound Oracle"*.