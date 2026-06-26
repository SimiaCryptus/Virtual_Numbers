# `nam/skip.hpp` — Skip-Ahead (Fast-Forwarding Automata)

## Overview

This header implements **M7: Skip-ahead** (Section 3.6 of the NAM
specification). The skip-ahead operation answers a deceptively simple
question: *given an automaton state `s`, what state results from applying
`n` transitions?* Naively this costs `O(n)` steps. For automata whose
orbits are **eventually periodic** or otherwise **fast-forwardable**, we can
do dramatically better — `O(1)` for genuine periodic orbits via phase
arithmetic, and `O(log n)` for linear-recurrence structures via repeated
squaring.

---

## Background and Theory

### Eventually periodic orbits

A discrete dynamical system `s ↦ f(s)` over a *finite* state space must, by
the pigeonhole principle, eventually revisit a state. The orbit therefore
decomposes into a **preperiod** (the "tail", or *μ* in Floyd/Brent
terminology) followed by a **cycle** of length *λ* (the "period"). This
"rho" (ρ) shape is the foundation of classical cycle-detection algorithms:

- **Floyd's tortoise-and-hare** (Knuth, *TAOCP* Vol. 2, §3.1, attributed to
  R. W. Floyd) detects a cycle with two pointers moving at different speeds.
- **Brent's algorithm** (R. P. Brent, *"An improved Monte Carlo
  factorization algorithm"*, BIT 20 (1980), 176–184) improves the constant
  factors and recovers *λ* and *μ* directly.

Once *μ* (preperiod) and *λ* (period) are known, advancing `n` steps reduces
to **phase arithmetic**:

```
if n < μ:      replay n steps in the tail
else:          replay μ steps to reach the cycle, then
               advance ((n − μ) mod λ) steps within the cycle
```

This is the principle exploited by `skip_rational` below.

### Rational expansions as periodic automata

The decimal (or *b*-ary) expansion of a rational number `p/q` is eventually
periodic — a fact known since antiquity and formalized by the theory of
repeating decimals. The relevant invariant is the **multiplicative order of
the base modulo the reduced denominator**: the period length divides
`ord_q(b)`, which in turn divides Euler's totient `φ(q)` (Euler–Fermat
theorem). The preperiod is governed by the factors of `q` shared with the
base `b`.

Crucially, it is the **sequence of remainders** (not the digits themselves)
that cycles deterministically: each remainder uniquely determines its
successor under long division, so the remainder stream is itself a finite
automaton orbit. We therefore precompute the cycle over remainders. See
`nam/rational.hpp` and Section 3.2 of the specification.

### *p*-adic rationals

The same eventual-periodicity holds for the *p*-adic expansion of a rational
(Hensel; see Koblitz, *p-adic Numbers, p-adic Analysis, and Zeta-Functions*,
Springer GTM 58). Phase-1 ships these periodic-orbit cases via
`nam/padic.hpp`.

### General fast-forwardable structure: repeated squaring

Many automata of interest are *not* tiny finite-state machines but are
nonetheless **fast-forwardable** because their transition is **linear** over
a modular ring. The canonical examples are:

- Linear congruential generators (Lehmer, 1949; see Knuth, *TAOCP* Vol. 2,
  §3.2.1), where jumping ahead `n` steps is a single modular exponentiation.
- Linear recurrences such as Fibonacci-type sequences, expressible as powers
  of a transition matrix.
- **BBP-style** digit-extraction formulae (Bailey, Borwein, Plouffe, *"On
  the rapid computation of various polylogarithmic constants"*, Math. Comp.
  66 (1997), 903–913), where individual digits are reachable without
  computing predecessors.

The unifying tool is **modular exponentiation by repeated squaring** (the
"square-and-multiply" / binary exponentiation method, attributed in essence
to the *Pingala*–*Chandaḥśāstra* and formalized in Knuth, *TAOCP* Vol. 2,
§4.6.3). For a `2×2` transition matrix `M` over `ℤ/mℤ`, computing `Mⁿ` takes
`O(log n)` matrix multiplications. This is the role of `Mat2`, `mat_mul`, and
`mat_pow`.

---

## API Reference

### `skip_naive<G>(n, s) → AutomatonVM`

The `O(n)` reference implementation: apply `G::step` exactly `n` times.

- **Role:** Correctness oracle for the fast paths and a fallback for VMs
  whose orbits are not (provably) periodic.
- **Template parameter `G`:** a `Generator` concept supplying
  `G::step(state).next`.

### `skip_rational(n, s) → AutomatonVM`

Advances a rational-expansion VM by `n` digits in roughly
`O(preperiod + log n)` time using the cycle of **remainders** computed by
`rational_period`.

Behaviour:

| Case                        | Result                                            |
|-----------------------------|---------------------------------------------------|
| `period == 0`, `n ≤ pre`    | naive replay (terminating expansion)              |
| `period == 0`, `n > pre`    | step to end of significant digits; remainder is 0 → all further digits are 0 |
| `period != 0`, `n < pre`    | naive replay of the preperiod prefix              |
| `period != 0`, `n ≥ pre`    | replay preperiod, then jump `(n − pre) mod period` within the cycle |

> **Note on terminating expansions.** A terminating *b*-ary expansion is the
> degenerate periodic case whose cycle is the single state `remainder = 0`,
> which reproduces digit `0` indefinitely.

### `struct Mat2`

A `2×2` matrix `[[a b],[c d]]` of `uint64_t` entries, representing the
transition of a linear recurrence modulo `m`.

### `mat_mul(x, y, m) → Mat2`

Modular matrix product `x · y (mod m)`. Intermediate products are computed in
`unsigned __int128` to avoid overflow before reduction — a standard guard for
`64×64 → 128`-bit modular multiplication.

### `mat_pow(base, e, m) → Mat2`

Modular matrix exponentiation `baseᵉ (mod m)` via square-and-multiply in
`O(log e)` multiplications. Seeded with the identity `[[1,0],[0,1]] (mod m)`.

---

## Design Notes and Phase Scope

- **Phase 1** ships the genuine periodic-orbit cases (rationals and *p*-adic
  rationals) plus the matrix-exponentiation kernel for demonstration and
  testing. The general fast-forwardable wiring (e.g. BBP-style extraction)
  builds on `mat_pow` in later phases.
- The `skip_naive` oracle exists specifically so the fast paths can be
  validated against an obviously-correct baseline.
- All modular arithmetic uses `unsigned __int128` intermediates; callers must
  ensure `m > 0`.

## Dependencies

- `nam/abi.h` — ABI and `AutomatonVM` definitions.
- `nam/generator.hpp` — the `Generator` concept.
- `nam/rational.hpp` — `Rational` VM and `rational_period`.
- `nam/padic.hpp` — *p*-adic rational VM.

## References

1. D. E. Knuth, *The Art of Computer Programming*, Vol. 2: *Seminumerical
   Algorithms*, §3.1 (cycle detection), §3.2.1 (linear congruential
   generators), §4.6.3 (evaluation of powers).
2. R. P. Brent, *An improved Monte Carlo factorization algorithm*, BIT 20
   (1980), 176–184.
3. D. H. Bailey, P. B. Borwein, S. Plouffe, *On the rapid computation of
   various polylogarithmic constants*, Math. Comp. 66 (1997), 903–913.
4. N. Koblitz, *p-adic Numbers, p-adic Analysis, and Zeta-Functions*,
   Springer GTM 58.
5. NAM Specification, Sections 3.2 and 3.6.