# `nam/refine.hpp` — Interval Refinement & Online Digit Extraction

## Overview

This header implements **Phase 2** of the NAM pipeline (see THEORY.md,
*"Interval Refinement Engine"*): converting a convergent series into a
stream of base-`b` digits in an **interval-honest** manner. Rather than
committing digits eagerly, the engine emits a digit only when *every*
value in the current enclosing interval agrees on that digit. This
guarantees that no emitted digit is ever wrong — at the cost of
occasionally returning a *pending* result (`std::nullopt`) for values
that sit exactly on a digit boundary.

## Background & Theory

### Exact real arithmetic and interval enclosures

The technique here belongs to the tradition of **exact real arithmetic**
(ERA), in which real numbers are represented not by fixed-precision
floating point but by procedures that can produce arbitrarily good
approximations on demand. Foundational treatments include:

- **Boehm, Cartwright, Riggle & O'Donnell (1986)**, *"Exact real
arithmetic: A case study in higher order programming"* — one of the
earliest demonstrations of lazy, on-demand real arithmetic.
- **Edalat & Potts (1997)** and the **interval / linear-fractional
transformation** school, which represent reals as nested shrinking
intervals (digit streams over a "signed digit" or LFT basis).
- **Weihrauch's Type-2 Theory of Effectivity (TTE)** (Weihrauch, 2000),
which provides the computability-theoretic foundation: a real is
*computable* iff there exists a machine producing a converging sequence
of rational intervals enclosing it.

Our `DigitExtractor` maintains a rational interval `[lo/scale, hi/scale]`
enclosing the unresolved fractional remainder. As more series terms are
consumed, the interval narrows monotonically. This is precisely the
*nested interval* model of a computable real.

### The table-maker's dilemma and digit commitment

The decision of *when* a digit may be safely emitted is the
**table-maker's dilemma** in miniature (see Kahan; and Muller et al.,
*Handbook of Floating-Point Arithmetic*, 2010). A value arbitrarily close
to a digit boundary (e.g. `0.4999…` vs `0.5000…` in base 10) may require
unboundedly many terms to resolve. ERA libraries resolve this with
**signed-digit** or **redundant** representations that permit later
correction. We instead take the conservative, *honest* route: if the
interval straddles a digit boundary within the refinement budget, we
report **pending** (`std::nullopt`) rather than guess. This mirrors the
semantics of *partial* computable functions in TTE — boundary (e.g.
dyadic/exactly-representable) inputs are legitimately undecidable in
finite time under a non-redundant representation.

### Online / lazy digit production

The "emit a digit as soon as it is determined" pattern is the **online
arithmetic** model (Trivedi & Ercegovac, 1977; Ercegovac & Lang, *Digital
Arithmetic*, 2004), where results are produced most-significant-digit
first as inputs are consumed. `next_digit` realizes this: it scales the
interval by the radix and emits `floor(b·lo/scale)` exactly when it
coincides with `floor(b·hi/scale)`.

## Representation

| Field        | Meaning |
|--------------|---------|
| `vm`         | The `SeriesVM` producing successive partial sums and a rigorous tail (truncation-error) bound. |
| `base`       | The output radix `b`. |
| `lo`, `hi`   | Integer numerators of the enclosing interval for the *remaining* fraction, over common denominator `scale`. Invariant: `0 ≤ lo ≤ hi`. |
| `scale`      | Positive common denominator (= `vm.den`). |
| `prefix`     | The integer value (in base `b`) of digits already committed. |
| `consumed`   | Count of committed digits. |

### Re-derivation invariant (interval honesty)

Rather than incrementally rescaling `lo`/`hi` after each emitted digit (a
procedure prone to drift and accumulated rounding of the *bounds*),
`sync_interval()` **re-derives the remaining fraction exactly** from the
VM at the current depth:

```
full = vm.num / vm.den              (lower bound of full value)
full_hi = (vm.num + tail) / vm.den  (upper bound; tail = rigorous error)

remaining_lo = base^consumed · full   − prefix
remaining_hi = base^consumed · full_hi − prefix
```

Multiplying by `base^consumed` shifts the still-unresolved fraction into
`[0, 1)`-scaled integer form, and subtracting `prefix · scale` removes the
already-emitted leading digits. Because every bound traces directly back
to the VM's *certified* `[num, num+tail]` enclosure, the interval is exact
at all times — there is no possibility of the bounds themselves
accumulating error.

## API

### `DigitExtractor make_extractor(SeriesVM vm, uint32_t base)`

Constructs an extractor for a fractional value in `[0, 1)`. The caller
must ensure the underlying series converges into `[0, 1)`; for whole
transcendentals (e.g. `e`), the integer and fractional parts are separated
upstream (see `constants.hpp`).

### `std::optional<uint32_t> next_digit(DigitExtractor& ex)`

Attempts to emit the next base-`b` digit.

- **Mechanism.** Scale by the radix: the candidate digit is
`floor(b·lo/scale)`. It is committable iff it equals `floor(b·hi/scale)`
— i.e. the entire interval lies within a single digit cell.
- **Refinement loop.** When the bounds disagree, `refine_terms_per_step`
additional series terms are stepped to shrink the interval, then the
loop re-syncs and retries.
- **Honest pending.** After `max_refine_iters` unsuccessful iterations,
returns `std::nullopt`. This is *not* an error: it is the correct answer
for a value lying on (or unresolvably near) a digit boundary.

On success, the digit is folded into `prefix` and `consumed` is
incremented, so the next call's `sync_interval()` re-derives the new
remainder.

### `std::vector<uint32_t> extract_digits(DigitExtractor ex, int n)`

Convenience wrapper emitting up to `n` digits, stopping early (returning
fewer than `n`) the first time `next_digit` reports pending. Takes the
extractor by value so the caller's state is left untouched.

## Tunable parameters

| Parameter                 | Default | Effect |
|---------------------------|---------|--------|
| `refine_terms_per_step`   | `1`     | Terms pulled per refinement attempt. Larger values reduce loop overhead for slowly-converging series at the cost of possible over-refinement. |
| `max_refine_iters`        | `4096`  | Upper bound on refinement attempts before honest pending. Bounds worst-case work on near-boundary values. |

## Correctness properties

1. **Soundness (no wrong digit).** A digit is emitted only when
`floor(b·lo/scale) == floor(b·hi/scale)`, so it equals the true digit
for *every* real in the certified enclosure — hence for the true value.
2. **Monotone refinement.** Stepping the VM never widens the certified
`[num, num+tail]` enclosure, so the interval is non-increasing.
3. **Honest partiality.** Termination with `std::nullopt` occurs only when
the interval cannot be resolved within budget — consistent with the
undecidability of exact boundaries under a non-redundant radix
representation (cf. TTE partial-function semantics).

## References

- Boehm, H., Cartwright, R., Riggle, M., O'Donnell, M. (1986). *Exact real
arithmetic: A case study in higher order programming.* ACM LISP &
Functional Programming.
- Weihrauch, K. (2000). *Computable Analysis: An Introduction.* Springer
(Type-2 Theory of Effectivity).
- Edalat, A., Potts, P. (1997). *A new representation for exact real
numbers.* Electronic Notes in Theoretical Computer Science.
- Ercegovac, M. D., Lang, T. (2004). *Digital Arithmetic.* Morgan
Kaufmann (online arithmetic, MSD-first production).
- Muller, J.-M. et al. (2010). *Handbook of Floating-Point Arithmetic.*
Birkhäuser (the table-maker's dilemma).

## See also

- `nam/series.hpp` — the `SeriesVM` providing certified `[num, num+tail]`
enclosures consumed here.
- `nam/big_int.hpp` — arbitrary-precision integers and `floordiv`.
- `THEORY.md` §"Interval Refinement Engine" — the design rationale.