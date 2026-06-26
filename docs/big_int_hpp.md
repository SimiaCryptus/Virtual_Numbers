# `nam::BigInt` — Vendored Arbitrary-Precision Signed Integer

*Header:* `include/nam/big_int.hpp`
*Tier:* Phase 2 — the SERIES tier
*Status:* Self-contained vendored implementation; GMP-compatible drop-in path reserved.

---

## 1. Purpose and Scope

The SERIES tier requires exact integer arithmetic whose operands grow without
a fixed machine-word bound. The implementation plan nominates
[GNU MP (GMP)](https://gmplib.org/) as the production backend, but to keep the
repository buildable with **zero external dependencies**, this header vendors a
minimal arbitrary-precision signed integer, `nam::BigInt`.

The vendored type deliberately mirrors only the slice of GMP semantics that the
series tier actually exercises:

- addition, subtraction, negation;
- multiplication;
- truncating and floored division with remainder;
- total ordering and equality;
- explicit deep copy;
- decimal serialization and bit-width instrumentation.

When `NAM_USE_GMP` is defined, a thin RAII wrapper around `mpz_t` can replace the
body of this header without touching any series-tier call site, because the public
surface is intentionally a subset of GMP's contract.

---

## 2. Theoretical Background

### 2.1 Sign-Magnitude Representation

`BigInt` uses a **sign-magnitude** representation: an unsigned magnitude paired
with a boolean sign flag. This is the oldest and most direct encoding of signed
integers (cf. Knuth, *TAOCP* Vol. 2, §4.1, *Positional Number Systems*), and it
is the representation GMP itself uses internally (`_mp_size` carries the sign as
the limb-count's sign). Sign-magnitude is chosen over two's-complement here because:

1. The magnitude algorithms (add/sub/mul/div) are naturally defined on unsigned
   values; sign handling factors cleanly into a thin dispatch layer.
2. There is no fixed width, so two's-complement's principal advantage — uniform
   overflow-free addition — confers no benefit.

The magnitude is stored little-endian in **base 2³²**, i.e. a
`std::vector<uint32_t>` of *limbs* (GMP terminology). Base 2³² is selected so that
the product of two limbs fits exactly in a `uint64_t`, allowing schoolbook
multiplication to proceed with portable 64-bit arithmetic and no compiler
intrinsics or 128-bit types. This is the classic "half-word base" trick described
in Knuth §4.3.1 and standard in multiple-precision packages.

### 2.2 Canonical Form

A value is kept in canonical form by `normalize()`:

- no leading-zero limbs (`mag_.back() != 0` unless empty);
- the empty magnitude vector denotes **zero**, and zero is always non-negative
  (`negative_ == false`).

Maintaining a unique canonical representation makes equality a direct
`(sign, magnitude)` comparison and removes the "negative zero" pathology that
plagues naïve sign-magnitude implementations.

### 2.3 The Core Algorithms

| Operation    | Algorithm                                        | Reference                                                                                         |
|--------------|--------------------------------------------------|---------------------------------------------------------------------------------------------------|
| `add_mag`    | Schoolbook ripple-carry addition                 | Knuth §4.3.1, Algorithm A                                                                         |
| `sub_mag`    | Schoolbook borrow subtraction (requires `a ≥ b`) | Knuth §4.3.1, Algorithm S                                                                         |
| `mul_mag`    | Schoolbook long multiplication, O(*mn*)          | Knuth §4.3.1, Algorithm M                                                                         |
| `divmod_mag` | Binary (bit-at-a-time) restoring division        | Knuth §4.3.1; Hennessy & Patterson, *Computer Architecture*, restoring-division hardware analogue |

The division routine is the **restoring** variant of binary long division: the
remainder accumulator is shifted left one bit per step, the corresponding dividend
bit is injected, and a trial subtraction commits only when the partial remainder
is ≥ the divisor. This is asymptotically O(*n*²) in the bit length and is
deliberately the simplest correct algorithm — adequate for Phase 2 operand sizes.
Knuth's Algorithm D (multi-limb normalized long division) and sub-quadratic methods
(Burnikel–Ziegler recursive division, Newton-iteration reciprocals) are the natural
upgrade path, but are intentionally deferred to the GMP backend.

### 2.4 Division Semantics: Truncated vs. Floored

Two division conventions are provided, reflecting a well-known distinction in the
literature on integer division (see Boute, R. T., *The Euclidean Definition of the
Functions div and mod*, ACM TOPLAS 14(2), 1992):

- **`divmod` / `operator/` / `operator%`** — *truncated* division (rounds the
  quotient toward zero). The remainder takes the **sign of the dividend**. This
  matches C99/C++11 `/` and `%` for built-in integers.
- **`floordiv`** — *floored* division (rounds the quotient toward −∞). The
  remainder takes the **sign of the divisor** and is therefore non-negative when
  the divisor is positive. This is the convention required for stable **digit
  extraction**, where a negative remainder would corrupt positional decomposition.

The floored form is derived from the truncated form by the standard correction:
when the remainder is non-zero and operands differ in sign, decrement the quotient
and add the divisor back into the remainder.

### 2.5 Exponentiation by Squaring

`big_pow` implements **binary exponentiation** (exponentiation by squaring),
computing `base^exp` in O(log *exp*) multiplications. The technique dates to the
*Chandaḥśāstra* of Piṅgala (c. 200 BCE) and is catalogued as the right-to-left
binary method in Knuth §4.6.3.

---

## 3. The Honesty Commitment: Deep-Copy-on-Fork

Per `THEORY.md`, the **deep-copy cost on fork is part of the public contract** of
the NAM system. The complexity accounting for the series tier claims an honest
O(log *n*) fork cost, and that claim is only meaningful if copies are genuinely
proportional to the data they duplicate.

`BigInt` therefore relies on the default copy semantics of `std::vector<uint32_t>`,
which performs an **eager, element-wise byte copy** of the limb buffer. There is
**no copy-on-write (COW)** and no reference-counted sharing. This is a deliberate
design decision:

> A COW representation would hide fork cost behind a deferred, amortized payment
> that surfaces unpredictably on the next mutation. That would make the published
> complexity bounds dishonest. The explicit deep copy keeps fork cost *visible*
> and *attributable* at the point of forking.

This is the single most important non-obvious property of the type and must be
preserved by any future GMP-backed implementation (note that modern GMP's `mpz_t`
copy via `mpz_init_set` is likewise an eager deep copy, so the contract is
naturally upheld).

 ---

## 4. Public Interface

### 4.1 Construction

| Constructor                          | Description                                 |
 |--------------------------------------|---------------------------------------------|
| `BigInt()`                           | Canonical zero.                             |
| `BigInt(int64_t v)`                  | From a signed 64-bit value.                 |
| `static BigInt from_u64(uint64_t v)` | From an unsigned 64-bit value (full range). |

The `int64_t` constructor computes the magnitude of `INT64_MIN` safely via the
`-(v + 1) + 1` idiom, avoiding the undefined behavior of negating the most-negative
two's-complement value directly.

### 4.2 Inspection

| Member                          | Returns                                                          |
 |---------------------------------|------------------------------------------------------------------|
| `bool is_zero() const`          | True iff the value is zero.                                      |
| `bool negative() const`         | Sign flag (always `false` for zero).                             |
| `int bit_width() const`         | Bit length of the magnitude — complexity-metric instrumentation. |
| `int64_t to_i64() const`        | Narrowing conversion; caller must assert it fits.                |
| `std::string to_string() const` | Decimal serialization.                                           |

`bit_width()` is exposed specifically so the complexity-accounting machinery can
measure operand growth, consistent with the THEORY commitment to observable cost.

### 4.3 Operators

- **Comparison:** `==`, `!=`, `<`, `<=`, `>`, `>=` — a total order respecting sign.
- **Arithmetic:** binary `+`, `-`, `*`; unary `-`; compound `+=`, `-=`, `*=`.
- **Division:** `/`, `%` (truncating); plus the static
  `divmod` and `floordiv` for explicit remainder access.

### 4.4 Free Functions

- `BigInt big_pow(const BigInt& base, uint64_t exp)` — binary exponentiation.

---

## 5. Complexity Summary

Let *n*, *m* be the limb counts of the operands.

| Operation       | Time                              | Space                           |
|-----------------|-----------------------------------|---------------------------------|
| Copy (fork)     | Θ(*n*)                            | Θ(*n*) — **eager, by contract** |
| Compare         | O(min(*n*, *m*))                  | O(1)                            |
| Add / Sub       | O(max(*n*, *m*))                  | O(max(*n*, *m*))                |
| Multiply        | O(*n·m*)                          | O(*n+m*)                        |
| Divmod          | O((*n*·32)²)-ish bitwise          | O(*n*)                          |
| `big_pow(b, e)` | O(log *e*) multiplications        | —                               |
| `to_string`     | O(*n*²) (repeated division by 10) | O(digits)                       |

The bitwise division and quadratic decimalization are the principal performance
cliffs; both are eliminated by the GMP backend and are acceptable at Phase 2 scale.

---

## 6. GMP Migration Path

To switch backends:

1. Define `NAM_USE_GMP`.
2. Replace the class body with an `mpz_t` RAII wrapper exposing the **identical**
   public signatures listed in §4.
3. Preserve the deep-copy-on-fork contract (§3) — naturally satisfied by
   `mpz_init_set` in copy construction/assignment.
4. Map `divmod` → `mpz_tdiv_qr` (truncating) and `floordiv` → `mpz_fdiv_qr`
   (floored), matching the semantics documented in §2.4.

No series-tier source requires modification, because callers depend only on the
documented subset.

 ---

## 7. References

- Knuth, D. E. *The Art of Computer Programming*, Vol. 2: *Seminumerical
  Algorithms*, 3rd ed. — §4.1 (positional systems), §4.3.1 (classical multiple-
  precision arithmetic), §4.6.3 (evaluation of powers).
- Boute, R. T. "The Euclidean Definition of the Functions div and mod."
  *ACM Transactions on Programming Languages and Systems* 14(2), 1992, pp. 127–144.
- Granlund, T. et al. *GNU MP: The GNU Multiple Precision Arithmetic Library*
  manual — internal limb representation and division routines.
- Burnikel, C. and Ziegler, J. "Fast Recursive Division." MPI-I-98-1-022, 1998
  (deferred sub-quadratic division reference).
- Hennessy, J. L. and Patterson, D. A. *Computer Architecture: A Quantitative
  Approach* — restoring division hardware (binary-division analogue).
- Project `THEORY.md` — the honesty commitment on fork cost.