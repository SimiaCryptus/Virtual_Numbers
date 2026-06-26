# `rational.hpp` — Rationals as Constant-State Periodic Virtual Machines

## Overview

This header realizes **M2** of the project: the treatment of rational numbers
as *constant-state periodic virtual machines* (Section 3.2). A rational number
`p/q`, expanded in a positional base `b`, is modeled not as a static value but
as a deterministic finite-state process that emits one digit per step while
occupying a fixed amount of memory.

The central observation is classical: the base-`b` expansion of any rational
number is **eventually periodic**, and conversely every eventually periodic
expansion denotes a rational. The machine here makes that theorem operational —
it *is* the long-division process, frozen into a constant-size register file.

## Theoretical Background

### Long division as a finite-state automaton

Computing the base-`b` digits of a fraction `r/q` (with `0 ≤ r < q`) proceeds
by the elementary long-division recurrence:

```
r₀ = p mod q
dₙ = ⌊(b · rₙ) / q⌋          (the n-th emitted digit)
rₙ₊₁ = (b · rₙ) mod q        (the carried remainder)
```

The only quantity that persists between steps is the remainder `rₙ`, and it
always satisfies `0 ≤ rₙ < q`. The denominator `q` and base `b` are fixed.
Therefore the *entire* state of the computation lives in a single register
(`state[0]`), and the machine never grows as digits are produced. This is what
is meant by **constant-state**: the logical register count is `1`, independent
of how many digits are emitted.

### Why the expansion is eventually periodic

Because each remainder lies in the finite set `{0, 1, …, q−1}`, the sequence
`r₀, r₁, r₂, …` can take at most `q` distinct values. By the pigeonhole
principle, some remainder must recur within the first `q + 1` steps. Once a
remainder repeats, the deterministic recurrence forces the *entire* subsequent
trajectory — and hence the digit stream — to repeat. This yields the standard
result:

> The base-`b` expansion of `p/q` is eventually periodic, with a pre-period and
> a period each of length at most `q`. The expansion terminates (period `0`)
> exactly when, after clearing common factors, `q` divides some power of `b`.

This is a textbook fact about positional number systems; see Hardy & Wright,
*An Introduction to the Theory of Numbers* (Ch. 9, decimals and continued
fractions), and the discussion of repeating expansions in Knuth, *The Art of
Computer Programming*, Vol. 2, §4.1 (positional number systems).

### Cycle detection and the role of `rational_period`

Detecting the period is precisely the problem of finding a repeated state in
the trajectory of a deterministic dynamical system. The function
`rational_period` performs a direct repeated-remainder search, returning the
pair `(preperiod, period)`. The same task can be solved in `O(1)` auxiliary
space with Floyd's or Brent's cycle-finding algorithms (Knuth, TAOCP Vol. 2,
§3.1, Exercise 6; Brent, *BIT* 20 (1980), 176–184); the implementation here
favors a transparent linear-memory scan with an explicit safety bound, which is
adequate for the modest denominators handled in this phase.

### Relation to the generator abstraction

`Rational` satisfies the project's `Generator` concept (verified by
`static_assert(Generator<Rational>)`), placing it on the same footing as the
other digit-emitting automata in the framework. The digit step is *pure*: it is
a `constexpr` function from one `AutomatonVM` to a `NumVMStep` (emitted digit
plus successor state), with no hidden mutation. This mirrors the coalgebraic /
stream view of infinite sequences as unfoldings of a state-transition function
(an *anamorphism*; cf. Bird & de Moor, *Algebra of Programming*, 1997), where
the carrier is the remainder and the observation is the digit.

## State Layout

The `AutomatonVM` register file is interpreted as follows:

| Field      | Meaning                                                        |
|------------|----------------------------------------------------------------|
| `base`     | Codec / digit alphabet size `b`.                               |
| `phase`    | Digit index `n` (informational; not required for stepping).    |
| `state[0]` | Running remainder `rₙ`, invariant `0 ≤ rₙ < q`.                 |
| `state[1]` | Denominator `q`.                                               |
| `state[2]` | Numerator `p` (recorded at construction; preserved unchanged). |

Only `state[0]` and `phase` evolve during stepping; the machine's memory
footprint is constant.

## API Reference

### `struct Rational`

```cpp
static constexpr NumVMStep Rational::step(AutomatonVM s);
```

Performs one long-division step. Implementation notes:

- Long division is realized *by multiplication*: `scaled = r · b`, then
`digit = scaled / q` and `next_r = scaled % q`.
- For sane inputs (`q ≲ 2³²`, small `base`), the product `r · b` fits in
`uint64_t`, since `r < q`.
- **Degenerate case** `q == 0`: the machine emits a stream of zero digits and
advances `phase`, rather than dividing by zero. This keeps the generator
total (every state has a successor).

### `make_rational(p, q, base) -> AutomatonVM`

Constructs a generator for the **fractional part** `(p mod q) / q` in the given
base. The integer part is intentionally discarded: this phase focuses on the
stream of digits to the *right* of the radix point. The initial remainder is
`p mod q` (or `0` when `q == 0`).

### `rational_period(vm) -> std::pair<uint64_t, uint64_t>`

Walks the remainder sequence until a remainder repeats, returning
`{preperiod_length, period_length}`:

- A return of `{idx, 0}` indicates a **terminating** expansion (a remainder of
`0` was reached at index `idx`).
- Otherwise `{i, idx − i}` gives the length of the pre-period and the length of
the repeating block.
- A safety bound of `1,000,000` iterations guards against pathological or
adversarial denominators; on hitting it the function reports `{idx, 0}`.

## Worked Example

Consider `1/7` in base `10`. The remainder trajectory is

```
1 → 3 → 2 → 6 → 4 → 5 → 1 → …
```

which repeats with no pre-period and period length `6`, producing the familiar
digits `142857142857…`. Here `rational_period(make_rational(1, 7, 10))` returns
`{0, 6}`. By contrast `1/8` in base `10` terminates, since `8 = 2³` divides
`10³`, and `rational_period` returns a period of `0`.

## References

- G. H. Hardy and E. M. Wright, *An Introduction to the Theory of Numbers*,
6th ed., Oxford University Press, 2008. (Periodicity of positional
expansions.)
- D. E. Knuth, *The Art of Computer Programming, Vol. 2: Seminumerical
Algorithms*, 3rd ed., Addison-Wesley, 1997. §4.1 (positional number systems),
§3.1 (cycle detection).
- R. P. Brent, "An improved Monte Carlo factorization algorithm," *BIT
Numerical Mathematics* 20 (1980), 176–184. (Space-efficient cycle finding.)
- R. Bird and O. de Moor, *Algebra of Programming*, Prentice Hall, 1997.
(Anamorphisms / unfolds as the categorical basis for stream generation.)