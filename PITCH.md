# numbers-as-machines (nam)

**Numbers are not data. Numbers are programs.**

`nam` is a header-only C++20 library that represents numbers as *forkable
digit-emitting state machines* rather than as fixed-width approximations or
inert big-number blobs. A number is a tiny virtual machine: you ask it for
its next digit, it hands you a digit and its successor state. Nothing is
rounded until *you* decide how much precision you want, and every answer the
library gives is honest about what it does and does not know.

---

## What it does

At its core `nam` exposes one frozen C ABI (`include/nam/abi.h`):

```c
typedef NumVMStep (*NumVMFn)(AutomatonVM);   /* step(state) -> (digit, next) */
```

Every number in the library is, ultimately, a function of this shape. From
that single primitive the library builds three layers:

### 1. The automaton tier — O(1) fork, fixed state

Numbers whose digit stream is driven by a finite-state recurrence live in a
`40`-byte POD struct (`AutomatonVM`). Forking such a number is a literal
struct copy. This tier includes:

- **Rationals** (`rational.hpp`) — `p/q` as a constant-state long-division
  machine, with exact period detection.
- **Quadratic irrationals** (`algebraic.hpp`) — `sqrt(D)`, `phi`, computed
  via the classic digit-by-digit square-root recurrence in any base.
- **p-adic numbers** (`padic.hpp`) — `a/b` in `Z_p`, with *local* digit
  commitment (LSB-up), the metric (`metric.hpp`), and valuations.
- **Skip-ahead** (`skip.hpp`) — jump `n` digits in `O(log n)` for periodic
  orbits via phase arithmetic and modular matrix exponentiation.

### 2. The series tier — O(log n) fork, growing state

Transcendentals like `e`, `ln 2`, and `1/e` (`constants.hpp`) are
represented as convergent series, each shipping its own **compiled
convergence proof** (a monotone tail-bound oracle). State here grows with
depth, so forking is an *explicit, instrumented deep copy* — never
copy-on-write — so the cost is visible and honest (`series.hpp`,
`big_int.hpp`).

Digits are committed through an **interval-refinement engine** (`refine.hpp`)
that emits a digit only when every value in the current bounding interval
agrees on it. Exact-boundary cases (e.g. `0.5` in base 10) honestly return
*pending* rather than guessing.

### 3. The user tier — ergonomic, honest API

`Number` (`number.hpp`) is a tagged union over both tiers offering an
mpmath/Decimal-flavoured surface:

- **Scoped, thread-local precision contexts** (no global mutable precision).
- **Explicit memoization** — `.streaming()` / `.cached(N)` (`memo.hpp`);
  there is no hidden global cache that would silently break fork semantics.
- **Base as a projection** — `in_base(b)` (`codec.hpp`); the base is a codec,
  not baked into the number's identity.
- **Interval-honest comparison** (`compare.hpp`) — `definitely_less_than`
  returns *true / false / pending*, never a false definite answer.

---

## What it solves

**The precision-is-baked-in problem.** With `float`/`double` you choose
precision *before* you know how much you need, and rounding error is silent.
With `nam` precision is a *consumer-side* decision: ask for as many digits as
the result needs, when you need them.

**The dishonest-equality problem.** Floating point lies about equality
(`0.1 + 0.2 != 0.3`); exact libraries can hang trying to decide undecidable
equalities. `nam` ships only *honest bounded predicates* — a comparison
either gives you a proven answer within your digit budget or admits it can't
yet tell.

**The expensive-fork problem.** Lazy / streaming numeric abstractions
usually make branching ("what's this value down two different code paths?")
either impossible or accidentally quadratic. `nam` makes **fork** a
first-class, cost-annotated operation: `O(1)` for the automaton tier,
`O(log n)` (and explicitly so) for the series tier.

**The hidden-state problem.** Global precision flags, implicit memo caches,
and copy-on-write sharing all leak state across logically independent values.
`nam` has *no global mutable state*: precision is scoped and thread-local,
caches are explicit and per-value, and forks are true value copies.

**The "base belongs to the number" problem.** Most libraries conflate a
number with the base it's printed in. `nam` treats base as a codec applied at
emission time, so the same value reprojects cleanly into any base.

---

## Alternative comparisons

| Concern                           | `double` / `float`  | GMP / MPFR                          | mpmath                 | Python `Decimal`        | Constructive reals (e.g. iRRAM, RealLib) | **nam**                                       |
|-----------------------------------|---------------------|-------------------------------------|------------------------|-------------------------|------------------------------------------|-----------------------------------------------|
| Precision chosen                  | Compile time, fixed | Per-operation, fixed                | Global mutable context | Context (mostly global) | On demand                                | **Consumer-side, scoped/thread-local**        |
| Rounding honesty                  | Silent error        | Explicit rounding mode              | Heuristic              | Explicit                | Provably correct                         | **Interval-honest + explicit pending**        |
| Equality / compare                | False positives     | Exact (can be wrong on irrationals) | Heuristic              | Exact for decimals      | Semi-decidable (can diverge)             | **Bounded honest tri-state**                  |
| Fork / branch cost                | Trivial copy        | Deep copy of big buffers            | Object copy            | Object copy             | Re-computation                           | **O(1) automaton / explicit O(log n) series** |
| Hidden state                      | —                   | —                                   | Global precision       | Global context          | —                                        | **None (scoped, explicit)**                   |
| Base handling                     | Binary only         | Binary internal                     | Binary internal        | Decimal only            | Binary internal                          | **Base = codec, any base**                    |
| Runtime expression specialization | —                   | —                                   | —                      | —                       | —                                        | **JIT to one NumVMFn**                        |
| External deps                     | None                | libgmp / mpfr                       | Python+SymPy           | Python stdlib           | Heavy runtime                            | **Zero by default; GMP optional**             |

**Versus GMP/MPFR.** Those give you fast fixed-precision big arithmetic, but
you still pick precision up front and pay full deep-copy on every branch.
`nam` defers precision to the point of consumption and makes branching cheap
where the math allows it.

**Versus mpmath / SymPy.** Excellent for interactive symbolic/numeric work,
but they rely on global precision context and heuristic comparison, and carry
a Python runtime. `nam` is a dependency-free C++ header library with no global
state and honest comparison built in.

**Versus constructive-reals libraries (iRRAM, RealLib, Boehm's CR).** These
share `nam`'s philosophy — compute as many digits as requested, refuse to lie
— and are the closest relatives. `nam`'s distinguishing bets are: a *frozen C
ABI* so numbers are literally function pointers; an explicit two-tier fork
cost model; a structural account where rationals, algebraics, and p-adics are
*small finite machines* with skip-ahead; first-class p-adic support; and a
JIT path that collapses runtime expression trees to a single specialized
step function.

**Versus `std::ratio` / boost::rational.** Those are exact rationals but
inert data with no streaming, no irrationals, no transcendentals, and no
honest comparison framework. `nam` treats a rational as one (small) member of
a much larger family of *number machines*.

---

## Design commitments (the honesty contract)

1. **No silent rounding.** Digits are committed only when provably correct;
   otherwise the answer is an explicit *pending*.
2. **Fork cost is part of the public contract.** It is annotated and, in the
   series tier, deliberately not copy-on-write.
3. **No hidden global state.** Precision is scoped/thread-local; memoization
   is opt-in and per-value.
4. **The ABI is frozen and versioned.** `AutomatonVM` is `40` bytes,
   trivially copyable, standard-layout — verified by `static_assert`.
5. **Zero required dependencies.** GMP are optional
   drop-in upgrades behind CMake flags.