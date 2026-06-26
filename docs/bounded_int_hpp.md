# `bounded_int.hpp` — Bounded-Width Unsigned Integer

## Overview

`nam::BoundedInt` is a minimal fixed-width (128-bit) unsigned integer used by
the automaton tier of the Phase 1 pipeline. It serves as a lightweight,
dependency-free stand-in for [`llvm::APInt`][apint], which the full
implementation plan ultimately targets (selectable via the
`NAM_USE_LLVM_APINT` build flag).

The design deliberately exposes `bit_width()` as a first-class observable so
that the complexity-metric instrumentation can verify the **logarithmic
growth** of register magnitudes: for a stream emitting `n` digits, the bit
width of the working register is expected to grow as `O(log n)`.

---

## Theoretical Background

### Arbitrary- and bounded-precision integers

The structure is a *positional, base-`2⁶⁴`* representation: a number is
stored as two 64-bit *limbs* (`lo`, `hi`) in little-endian order, so that
the represented value is

```
    value = hi · 2⁶⁴ + lo,   0 ≤ value < 2¹²⁸.
```

This is the standard *limb* (or *digit*) representation underlying
arbitrary-precision libraries such as **GMP** (the GNU Multiple Precision
Arithmetic Library) and LLVM's `APInt`. Knuth's treatment of multiple-
precision arithmetic in *The Art of Computer Programming*, Vol. 2,
§4.3.1 ("The Classical Algorithms") is the canonical reference for the
add/subtract/multiply/divide routines implemented here. The choice of a
fixed two-limb width is a deliberate *bounding* of that general scheme,
justified by the analysis in the automaton tier showing that remainders for
rational sources and degree-2 recurrence registers remain well below `2¹²⁸`.

### Bit width and the cost of digits

`bit_width()` returns `⌊log₂(value)⌋ + 1`, the number of significant bits
(with `0` mapping to width `0`). This is the information-theoretic size of the
integer and is the natural cost measure when reasoning about the space a
register consumes. The implementation uses a *count-leading-zeros* primitive
(`std::countl_zero`, C++20 `<bit>`) which compiles to a single hardware
instruction (`LZCNT`/`BSR` on x86, `CLZ` on ARM) — see Warren,
*Hacker's Delight*, 2nd ed., §5-3, for the underlying bit-twiddling theory.

The relationship between digit index and bit width is the crux of the
instrumentation contract: emitting the `n`-th digit of a source whose register
obeys a contraction/expansion bound should leave `bit_width()` scaling like
`Θ(log n)`, consistent with the standard result that representing an integer
of magnitude `m` requires `Θ(log m)` bits.

---

## Algorithms

### Addition and subtraction

`operator+` and `operator-` implement schoolbook carry/borrow propagation
across the two limbs (Knuth, *TAOCP* Vol. 2, Algorithms 4.3.1A and 4.3.1S).
The carry-out of the low limb is detected via the classic wrap-around test
`(lo < a.lo)`, valid for unsigned modular arithmetic; the borrow is detected
symmetrically with `(a.lo < b.lo)`.

### Multiplication by a small factor

`mul_small(f)` multiplies the value by a factor `f ≤ 2³²`. To avoid overflow
in the 64×64 → 128 partial products on platforms without a native 128-bit
integer type, `lo` is split into two 32-bit halves and the products are
recombined — a direct application of the *operand-scanning* (schoolbook)
multiplication described in Knuth, *TAOCP* Vol. 2, Algorithm 4.3.1M.

This primitive supports the `remainder ← remainder · base` step that drives
digit extraction in positional base conversion (e.g. the radix-`base`
rendering of a rational `p/q`).

### Division: restoring long division

`divmod(d, rem)` performs **restoring binary long division** by a 64-bit
divisor `d`, producing a quotient and remainder. The loop scans the 128 bits
of the dividend from most- to least-significant, shifting the partial
remainder left, OR-ing in the next dividend bit, and conditionally
subtracting the divisor:

```
    for i = 127 downto 0:
        rem = (rem << 1) | bit(i)
        if rem ≥ d:
            rem = rem − d
            quotient[i] = 1
```

This is the textbook restoring-division algorithm (Hennessy & Patterson,
*Computer Architecture: A Quantitative Approach*, Appendix J, "Division";
also Knuth, *TAOCP* Vol. 2, Algorithm 4.3.1D for the multi-limb general
case). Because `d` fits in 64 bits, the partial remainder also fits in 64
bits, so no 128-bit intermediate is required and each iteration is constant
work, giving an overall `O(w)` cost where `w = 128` is the fixed width.

### Bit access helpers

`bit_at(i)` and `set_bit(i)` provide random read/write access to individual
bits across the limb boundary, dispatching on the threshold `i ≥ 64`. These
underpin the division loop and mirror the bit-vector accessors found in
`APInt::operator[]` / `APInt::setBit`.

---

## Interface Summary

| Member                       | Description                                              |
|------------------------------|---------------------------------------------------------|
| `BoundedInt()`               | Zero-initialised value.                                  |
| `BoundedInt(uint64_t v)`     | From a single 64-bit value (`hi = 0`).                   |
| `BoundedInt(hi, lo)`         | From explicit limbs.                                     |
| `is_zero()`                  | True iff the value is `0`.                               |
| `bit_width()`                | Number of significant bits (`0` for zero).              |
| `operator== / < / <=`        | Lexicographic comparison on `(hi, lo)`.                  |
| `operator+ / -`              | Modular 128-bit add / subtract with carry / borrow.     |
| `mul_small(f)`               | Multiply by `f ≤ 2³²`, staying within 128 bits.         |
| `divmod(d, rem_out)`         | Restoring long division by 64-bit `d`; returns quotient.|
| `bit_at(i) / set_bit(i)`     | Single-bit read / write across the limb boundary.       |

All operations are `constexpr`, allowing compile-time evaluation and
use in `static_assert`-based unit tests.

---

## Design Notes & Caveats

- **Bounded, not arbitrary, precision.** Operations silently wrap modulo
  `2¹²⁸`. Callers in the automaton tier are responsible for staying within
  the analytically established bound; this is intentional and matches the
  "bounded register" model of the complexity instrumentation.
- **Divisor restricted to 64 bits.** `divmod` accepts a `uint64_t` divisor;
  this is sufficient for the remainder/base recurrences of Phase 1.
- **`mul_small` factor restricted to `2³²`.** Larger factors may overflow the
  high-limb computation; use repeated multiplication or the LLVM path for
  wider needs.
- **Drop-in `APInt` path.** Defining `NAM_USE_LLVM_APINT` is intended to
  replace this vendored type with [`llvm::APInt`][apint], preserving the
  `bit_width()` observable that the metrics layer relies upon.

---

## References

- D. E. Knuth, *The Art of Computer Programming, Vol. 2: Seminumerical
  Algorithms*, 3rd ed., §4.3.1 — classical multiple-precision algorithms.
- H. S. Warren, *Hacker's Delight*, 2nd ed., §5-3 — counting leading zeros
  and bit-width computation.
- J. L. Hennessy & D. A. Patterson, *Computer Architecture: A Quantitative
  Approach*, Appendix J — hardware division algorithms (restoring/non-
  restoring).
- T. Granlund et al., *The GNU Multiple Precision Arithmetic Library (GMP)* —
  limb-based arbitrary-precision representation.
- LLVM Project, [`llvm::APInt`][apint] — the production target this type
  shadows.

[apint]: https://llvm.org/doxygen/classllvm_1_1APInt.html