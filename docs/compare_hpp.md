# `nam/compare.hpp` — Interval-Honest Comparison

**Module:** M6 (Section 3.7)
**Header:** `include/nam/compare.hpp`

---

## 1. Background and Motivation

### 1.1 The Undecidability of Real Equality

A foundational result of computable analysis is that **equality of real
numbers is undecidable**. If a real number is represented by an algorithm
that emits an arbitrarily long stream of digits (or, more generally, a
sequence of converging rational intervals), then no terminating procedure
can decide whether two such representations denote the same real.

This is a direct consequence of the **non-existence of a total
discontinuous computable function**: equality `x == y` is a discontinuous
predicate (it jumps at the diagonal `x = y`), and every computable function
on the reals — under the standard Type-2 Theory of Effectivity (TTE)
representation of Weihrauch and others — is necessarily continuous. See:

- K. Weihrauch, *Computable Analysis: An Introduction*, Springer, 2000.
- E. Bishop and D. Bridges, *Constructive Analysis*, Springer, 1985.
- A. M. Turing, "On Computable Numbers, with an Application to the
  Entscheidungsproblem," *Proc. London Math. Soc.*, 1936 — the original
  source of the digit-stream model of a computable real.

The intuition: to certify `x == y` one would have to inspect *infinitely*
many digits, since two reals may agree on the first `N` digits for any
finite `N` yet still differ. Conversely, *inequality* is **semi-decidable**:
if `x ≠ y`, a difference must eventually appear at some finite digit
position, and a search will find it. This asymmetry is the cornerstone of
the present module.

### 1.2 Honest Predicates and the Three-Valued Logic

Because we cannot honestly return a `bool` for equality or strict ordering
over unbounded precision, this module ships only **bounded-precision,
interval-honest** predicates. Rather than pretend to a decision we cannot
make, we expose a third logical value — *pending* / *indistinguishable* —
when a definite answer is not reachable within the budgeted precision.

This is the constructive **trichotomy** of Bishop: for reals `x, y` and a
positive separation `ε`, one can constructively decide *one of*
`x < y`, `y < x`, or `|x − y| < ε`, but **not** the sharp trichotomy
`x < y ∨ x = y ∨ x > y`. Our `Trit` type is the computational realization
of the former, with `max_digits` playing the role of the precision bound
that fixes `ε`.

The connection to **Kleene's three-valued logic** and to the **Σ⁰₁ /
semi-decidable** structure of order predicates on computable reals is also
relevant: `definitely_less_than` is a *verifier* for the open predicate
`x < y`, returning a positive witness when one exists within budget, and
abstaining (`std::nullopt`) otherwise — never a false definite answer.

---

## 2. API Reference

### 2.1 `enum class Trit`

```cpp
enum class Trit { Less, Greater, Indistinguishable };
```

The three-valued result of a bounded comparison.

| Value               | Meaning                                                        |
| ------------------- | -------------------------------------------------------------- |
| `Less`              | `x` is **provably** less than `y` within the digit budget.     |
| `Greater`           | `x` is **provably** greater than or equal-then-greater than `y`.|
| `Indistinguishable` | No definite ordering established within `max_digits` (pending). |

`Indistinguishable` does **not** assert equality — equality is undecidable.
It asserts only that the two streams agreed on every inspected digit, so the
operands are indistinguishable *at the requested precision*.

---

### 2.2 `agrees_with`

```cpp
template <Generator G>
bool agrees_with(AutomatonVM x, AutomatonVM y, int digits);
```

Returns `true` iff the digit streams produced by `G` for `x` and `y` are
**exactly equal on the finite prefix** of length `digits`.

- This is the honest analogue of equality: it certifies *agreement to a
  stated precision*, never genuine real-number equality.
- Complexity: `O(digits)` generator steps.

> **Theoretical note.** `agrees_with(..., n)` is a member of the family of
> approximations `xₙ ≈ yₙ` whose limit `n → ∞` is the undecidable predicate
> `x = y`. Each member is decidable; the limit is not.

---

### 2.3 `definitely_less_than`

```cpp
template <Generator G>
std::optional<bool> definitely_less_than(AutomatonVM x, AutomatonVM y,
                                         int max_digits);
```

Scans up to `max_digits` digit positions, most-significant first.

| Return                  | Interpretation                                              |
| ----------------------- | ---------------------------------------------------------- |
| `true`                  | `x` is provably `< y` (first differing digit had `x < y`). |
| `false` (engaged)       | `x` is provably `≥`-then-`>` (first differing digit `x > y`).|
| `std::nullopt`          | Pending / indistinguishable within `max_digits`.           |

The contract is **soundness without completeness**: an engaged result is
always correct; the *absence* of a result (`nullopt`) is never a false
negative — it simply means the witness, if any, lies beyond the budget.

> **⚠ Representation caveat.** This predicate assumes a
> **most-significant-first (MSF)** positional digit stream, as used for reals
> and rationals in a fixed base `b`. The lexicographic comparison of MSF
> digit prefixes is order-isomorphic to the numeric order. It is **not
> valid** for **least-significant-first (LSB-up) p-adic** streams, where the
> p-adic valuation/ultrametric order has no such lexicographic-from-the-front
> correspondence. For p-adic comparison, use the dedicated p-adic ordering
> facilities instead.

---

### 2.4 `compare`

```cpp
template <Generator G>
Trit compare(AutomatonVM x, AutomatonVM y, int max_digits);
```

A thin convenience wrapper over `definitely_less_than` that lifts the
`std::optional<bool>` result into the three-valued `Trit`:

- `nullopt` → `Trit::Indistinguishable`
- `true`    → `Trit::Less`
- `false`   → `Trit::Greater`

This is the recommended entry point for application code that wants the
full honest trichotomy in a single value.

---

## 3. Design Rationale

1. **No lies.** We never return a `bool` claiming `x == y` or unbounded
   `x < y`; both are uncomputable in general. Every answer is qualified by a
   finite precision budget (`digits` / `max_digits`).
2. **Sound, partial.** Positive ordering answers are total-order-correct.
   Failure to answer is an explicit `nullopt` / `Indistinguishable`, never
   a silent incorrect verdict.
3. **Generator-parameterized.** The functions are templated on a
   `Generator G` (see `nam/generator.hpp`), decoupling the comparison logic
   from any particular numeric stream implementation.
4. **Constant memory.** All routines stream digits and carry only the two
   current `AutomatonVM` states, requiring `O(1)` auxiliary space.

---

## 4. References

- A. M. Turing (1936). *On Computable Numbers…* — digit-stream reals,
  undecidability of equality.
- E. Bishop, D. Bridges (1985). *Constructive Analysis* — constructive
  trichotomy, `ε`-separation.
- K. Weihrauch (2000). *Computable Analysis* — TTE, continuity of computable
  functions, semi-decidability of `<`.
- S. C. Kleene (1952). *Introduction to Metamathematics* — three-valued
  logic and partial predicates.

---

## 5. See Also

- `nam/generator.hpp` — the `Generator` concept and `NumVMStep` digit model.
- `nam/abi.h` — `AutomatonVM` state type.