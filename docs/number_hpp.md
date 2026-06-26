# `nam/number.hpp` — The User-Facing Numeric API

## Overview

This header defines `nam::Number`, the user-facing layer of the NAM
(Numbers-as-Machines) system. It sits at the **combinator/user
boundary**: combinator authors work below it against the raw `NumVMFn`
ABI, while end users work above it through operator-style ergonomics,
precision contexts, explicit memoization, and honest comparison
predicates.

The design draws its surface vocabulary from two well-established
arbitrary-precision systems — Python's `mpmath` and the IEEE 854 /
`decimal` family — but departs from both in three deliberate ways that
we call the **honesty commitments**.

---

## Theoretical Background

### Numbers as machines (coinductive streams)

The core idea is that a real number is not a completed object but a
*process* that emits digits on demand. This is the **coinductive** or
**stream** view of real numbers, formalized in exact real arithmetic
research:

- **Brouwer / intuitionistic reals** — a real is given by a converging
  sequence of approximations, never by a finished decimal.
- **Exact real arithmetic (ERA)** — Boehm, Cartwright, Riggle &
  O'Donnell, *"Exact Real Arithmetic: A Case Study in Higher-Order
  Programming"* (1986); Vuillemin, *"Exact Real Computer Arithmetic with
  Continued Fractions"* (1990); Edalat & Potts, *"A New Representation
  for Exact Real Numbers"* (1997).
- **Coinduction and corecursion** — Bertot & Komendantskaya, and the
  general treatment of streams as final coalgebras (Rutten, *"Universal
  Coalgebra: A Theory of Systems"*, 2000).

In NAM, each `Number` is backed by a small state machine
(`AutomatonVM`) or a series virtual machine (`SeriesVM`) whose `step`
function is exactly the *next-digit* corecursive unfolding.

### Two tiers: automata vs. series

`Number` is a tagged union over two computational tiers, distinguished by
their **fork cost**:

| Tier        | Backing              | Fork cost  | Examples               |
|-------------|----------------------|------------|------------------------|
| `Automaton` | fixed-size DFA-like  | `O(1)`     | rationals, √D, p-adics |
| `Series`    | growing accumulators | `O(log n)` | e, ln 2, 1/e           |

- The **automaton tier** corresponds to *eventually periodic* or
  *finite-state* digit streams. Rational numbers in any base are
  eventually periodic (a classical result; see Hardy & Wright, *An
  Introduction to the Theory of Numbers*, on repeating expansions), and
  quadratic irrationals √D have periodic **continued fraction**
  expansions (Lagrange's theorem). Both admit fixed-size state, hence
  constant-cost copying.
- The **series tier** corresponds to transcendentals whose digit
  extraction relies on accumulating partial sums (e.g. the
  factorial-base series for *e*). Forking such a machine requires copying
  growing accumulators, giving the honest `O(log n)` annotation.

### Interval-honest comparison

A fundamental result of computable analysis is that **exact equality of
reals is undecidable**, and order comparison is only *semi-decidable*:
you can confirm strict inequality once the approximations separate, but
you can never confirm equality from a finite prefix.

- See Weihrauch, *Computable Analysis: An Introduction* (2000), on the
  non-computability of the equality and order tests on `R`.
- This is why `definitely_less_than` returns
  `std::optional<bool>` (a tri-state) and `compare` returns a `Trit`
  (`Less` / `Greater` / `Indistinguishable`). A `nullopt` /
  `Indistinguishable` result is the *honest* answer "not yet
  distinguishable within the given digit budget" — never a fabricated
  definite verdict.

The restriction documented on `definitely_less_than` (valid for
**MSB-first** positional streams, **not** for LSB-up p-adics) reflects
that lexicographic digit comparison only induces the real order when
digits arrive most-significant-first. p-adic numbers carry the
ultrametric topology (Gouvêa, *p-adic Numbers: An Introduction*, 1997),
where this comparison is meaningless.

---

## The Three Honesty Commitments

These are preserved verbatim from the substrate and enforced at the API
surface:

1. **Comparison is interval-honest.** No predicate ever returns a false
   definite answer; ambiguity is surfaced as `nullopt` / `Trit`.
2. **Fork cost is annotated by tier.** `fork_cost()` reports `"O(1)"`
   for automata and `"O(log n)"` for series, so users reason about
   complexity rather than guessing.
3. **Memoization is explicit.** There is no hidden global cache. A user
   opts in with `.cached(N)` (an LRU digit cache of bounded size) or
   chooses `.streaming()` for zero retained state.

Contrast with `mpmath`, which uses a **global mutable precision** (`mp.dps`)
and an implicit working context; NAM instead uses a scoped, thread-local
`PrecisionContext` so that value semantics are never violated across
scopes or threads.

---

## API Reference

### `PrecisionContext`

An RAII guard establishing a target digit accuracy for a region of code.

```cpp
{
    auto guard = nam::precision_context(50);
    // PrecisionContext::digits() == 50 inside this scope
} // previous precision restored on scope exit
```

- **Thread-local, not global-shared.** Each thread owns its own
  precision (`static thread_local int`), defaulting to `30` digits.
- Non-copyable, non-assignable; lifetime *is* the scope. This mirrors the
  `with precision_context(digits=N):` idiom from `mpmath`/`decimal` but
  removes the cross-scope leakage.

### `Number`

A thin tagged union exposing a uniform digit-stream surface.

#### Nested enums

- `Tier { Automaton, Series }` — computational tier.
- `Gen { Rational, Sqrt, PAdic }` — automaton generator family.
- `Memo { Streaming, Cached }` — memoization policy.

#### Construction

| Factory                        | Tier      | Notes                         |
|--------------------------------|-----------|-------------------------------|
| `Number::rational(p, q, base)` | Automaton | eventually-periodic expansion |
| `Number::sqrt(D, base)`        | Automaton | quadratic irrational          |
| `Number::padic(a, b, p)`       | Automaton | LSB-up p-adic stream          |
| `Number::e(base)`              | Series    | transcendental                |
| `Number::ln2(base)`            | Series    | transcendental                |
| `Number::one_over_e(base)`     | Series    | transcendental                |
| `Number::series(SeriesVM)`     | Series    | raw series escape hatch       |

Free-function ergonomic aliases (`mpmath`-flavoured) are also provided:
`make_e_number`, `make_ln2_number`, and `nam::sqrt`.

#### Introspection

- `tier()` — which tier backs this number.
- `base()` — the radix of the emitted digit stream.

#### `in_base(new_base)` — base as projection

Reprojects the number into a new radix (THEORY.md *"in_base(b)"*). Only
the **rational** fast path is analytic (`rational_in_base`); other
automaton generators carry their base in the VM and are simply re-seeded,
and series VMs carry the base directly. This treats the base as a
*projection* of the underlying number rather than an intrinsic property —
consistent with the stream view, where the radix is a rendering choice.

#### Memoization

- `.streaming()` — drop any cache; pure streaming, no retained digits.
- `.cached(max_digits)` — install a bounded `LruDigitCache`.

Caches are **never shared across forks**: `fork()` gives each branch an
independent cache, preserving value semantics.

#### `fork()` — value-semantic duplication

Returns `std::pair<Number, Number>`, two independent continuations of the
same stream. The automaton branch copies in `O(1)` (`num_vm_fork`); the
series branch performs an explicit `O(log n)` deep copy (`series_.fork()`).
The honest cost is queryable via `fork_cost()`.

#### `skip(n)` — skip-ahead

Returns `std::optional<Number>` advancing the stream by `n` digits. Only
meaningful for **periodic automata** — currently the rational path
(`skip_rational`). Returns `nullopt` for series and non-rational
automata, since arbitrary skip-ahead has no closed form there.

#### Digit emission

- `next_digit()` → `std::optional<uint32_t>`. Engaged = a committable
  digit; `nullopt` = an honest *pending* stall (only the series tier can
  stall, at exact-boundary inputs). Emitted digits are fed to the cache
  via `memo_put`.
- `digits(n)` — up to `n` digits (may return fewer on a pending stall).
- `digits()` — exactly `PrecisionContext::digits()` digits.

The series tier lazily constructs a `DigitExtractor` over a forked
`SeriesVM` so the original number's position is not disturbed.

#### Comparison predicates

- `agrees_with(other, digits)` — exact agreement over a finite prefix.
  A pending stall on either side yields `false` (cannot confirm
  agreement).
- `definitely_less_than(other, max_digits)` → `std::optional<bool>` —
  tri-state strict-order test (see *Interval-honest comparison* above).
- `compare(other, max_digits)` → `Trit` — `Less` / `Greater` /
  `Indistinguishable`.

Both order predicates operate on **forked copies** so they are
non-destructive.

#### Rendering

- `to_string(digits_n)` — renders `"0.<digits>"` in the current base
  (bases ≤ 36 use `0-9a-z`). A trailing `'?'` honestly marks a pending
  (series-boundary) stall; an out-of-range digit renders as `'#'`.

---

## Design Notes and Cross-References

- **Substrate ABI:** `nam/abi.h` (`NumVMFn`, `NumVMStep`).
- **Generators:** `nam/rational.hpp`, `nam/algebraic.hpp`,
  `nam/padic.hpp`, `nam/series.hpp`, `nam/constants.hpp`.
- **Comparison:** `nam/compare.hpp` (`Trit`), whose MSB-first restriction
  `definitely_less_than` faithfully reproduces.
- **Skip-ahead:** `nam/skip.hpp` (`skip_rational`).
- **Codec:** `nam/codec.hpp` (`rational_in_base`).
- **Memoization:** `nam/memo.hpp` (`LruDigitCache`).

The boundary discipline is intentional: **combinator authors stay below
`Number`**, composing raw VMs; **end users stay above it**, never seeing
the `NumVMFn` ABI. This separation echoes the classic distinction in ERA
libraries between the kernel (lazy/streaming arithmetic core) and the
user-facing numeric façade.

## Selected References

- H.-J. Boehm et al., *Exact Real Arithmetic: A Case Study in
  Higher-Order Programming*, LFP 1986.
- J. Vuillemin, *Exact Real Computer Arithmetic with Continued
  Fractions*, IEEE Trans. Computers, 1990.
- A. Edalat & P. J. Potts, *A New Representation for Exact Real Numbers*,
  ENTCS, 1997.
- K. Weihrauch, *Computable Analysis: An Introduction*, Springer, 2000.
- J. J. M. M. Rutten, *Universal Coalgebra: A Theory of Systems*, TCS,
    2000.
- F. Q. Gouvêa, *p-adic Numbers: An Introduction*, Springer, 1997.
- G. H. Hardy & E. M. Wright, *An Introduction to the Theory of Numbers*.