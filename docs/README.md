# NAM — Numbers as Machines

> *A number is not a value. It is a process that emits digits on demand.*

**NAM** is a C++20 library that represents real, rational, algebraic, and
*p*-adic numbers as **digit-emitting automata** rather than as stored
values. A number is a small, copyable state machine whose `step` function
unfolds one digit at a time — the *coinductive* / *stream* view of numbers
formalized in exact real arithmetic and computable analysis.

---

## Core Idea

Following the coalgebraic ("final coalgebra of streams") view of numbers,
every NAM number is backed by a transition function:

```
step(AutomatonVM) -> (digit, AutomatonVM)
```

This single Mealy-machine signature unifies archimedean and
non-archimedean number systems under one dispatch ABI. Because state is a
**flat, trivially-copyable value**, forking a number is an O(1) `memcpy`
— enabling cheap backtracking, speculative evaluation, and parallel
exploration of digit streams.

---

## The Three Honesty Commitments

NAM departs from conventional arbitrary-precision libraries (e.g. `mpmath`,
`decimal`) in three deliberate ways:

1. **Comparison is interval-honest.** Equality of reals is *undecidable*
   and order is only *semi-decidable*. NAM never fabricates a definite
   verdict: comparison returns a three-valued `Trit`
   (`Less` / `Greater` / `Indistinguishable`) and order tests return
   `std::optional<bool>`.

2. **Fork cost is annotated by tier.** `fork_cost()` reports `"O(1)"` for
   automata and `"O(log n)"` for series. Deep copies on fork are part of
   the public contract — never hidden behind copy-on-write.

3. **Memoization is explicit.** There is no hidden global cache. Users
   opt in with `.cached(N)` (a bounded LRU digit cache) or choose
   `.streaming()` for zero retained state.

---

## Two Tiers

NAM numbers fall into two computational tiers, distinguished by their
fork cost:

| Tier        | Backing              | Fork cost  | Examples               |
|-------------|----------------------|------------|------------------------|
| `Automaton` | fixed-size, DFA-like | `O(1)`     | rationals, √D, p-adics |
| `Series`    | growing accumulators | `O(log n)` | e, ln 2, 1/e           |

- The **automaton tier** models *eventually periodic* or *finite-state*
  digit streams. Rationals are eventually periodic in every base;
  quadratic irrationals (√D) have periodic continued fractions; p-adic
  rationals have ultimately periodic Hensel expansions.

- The **series tier** models transcendentals via convergent series paired
  with a certified **tail-bound oracle** (a compiled convergence proof).
  Digits are committed only by interval refinement — never guessed.

---

## Quick Start

```cpp
#include "nam/number.hpp"

using namespace nam;

// Rational 1/7 in base 10 → 0.142857142857...
Number r = Number::rational(1, 7, 10);
std::string s = r.to_string(12);   // "0.142857142857"

// Square root of 2 (fractional digits), base 10
Number root2 = nam::sqrt(2, 10);

// Euler's number as a series-tier transcendental
Number e = Number::e(10);

// Interval-honest comparison
auto guard = nam::precision_context(50);
Trit cmp = root2.compare(r, /*max_digits=*/50);

// O(1) value-semantic fork (independent continuations)
auto [a, b] = r.fork();
```

---

## Architecture

NAM is organized as a layered pipeline:

### The Frozen ABI — `nam/abi.h`

A C-compatible, versioned, POD ABI. `AutomatonVM` is exactly 40 bytes
(`base + phase + state[4]`), trivially copyable and standard-layout, so
that **fork is a literal struct copy**.

### Phase 1 — The Automaton Tier

| Header                | Module | Role                                      |
|-----------------------|--------|-------------------------------------------|
| `nam/generator.hpp`   | —      | The `Generator` concept + `take` / `fork` |
| `nam/rational.hpp`    | M2     | Rationals as constant-state periodic VMs  |
| `nam/algebraic.hpp`   | M3     | √D via digit-by-digit extraction          |
| `nam/padic.hpp`       | M5     | p-adic Hensel expansions (LSB-up)         |
| `nam/codec.hpp`       | —      | Base-as-codec reprojection                |
| `nam/skip.hpp`        | M7     | Skip-ahead via phase arithmetic / mat-pow |
| `nam/bounded_int.hpp` | —      | Fixed-width 128-bit register              |

### Phase 2 — The Series Tier

| Header              | Role                                          |
|---------------------|-----------------------------------------------|
| `nam/big_int.hpp`   | Vendored arbitrary-precision signed integer   |
| `nam/series.hpp`    | `SeriesVM` + tail-bound oracle (`SeriesSpec`) |
| `nam/constants.hpp` | Standard repertoire: e, ln 2, 1/e             |
| `nam/refine.hpp`    | Interval-honest online digit extraction       |
| `nam/memo.hpp`      | Explicit, bounded LRU memoization             |

### Comparison & Metric (M6)

| Header            | Role                                   |
|-------------------|----------------------------------------|
| `nam/compare.hpp` | Interval-honest `Trit` comparison      |
| `nam/metric.hpp`  | p-adic distance as a product automaton |

### Phase 4 — Runtime Specialization

| Header         | Role                                            |
|----------------|-------------------------------------------------|
| `nam/expr.hpp` | Runtime expression trees → `(GenTag, seed)`     |
| `nam/jit.hpp`  | `compile(expr) -> NumVMFn` (LLVM ORC or interp) |

### User Façade

| Header           | Role                         |
|------------------|------------------------------|
| `nam/number.hpp` | The user-facing `Number` API |

---

## Key Design Principles

- **State is a value.** By giving up `co_yield` coroutine syntax, NAM
  recovers cheap copying, compile-time `constexpr` evaluation, and safe
  speculative branching.

- **Base is a codec, not state.** A number's identity is independent of
  its radix. `in_base(b)` reprojects the *same* number into a new digit
  stream; for rationals this is an analytic re-seeding.

- **No lies.** Every answer is qualified by an explicit precision budget.
  Pending results are surfaced honestly as `std::nullopt` /
  `Indistinguishable`, never as a silent fabrication.

- **Honest cost accounting.** Memory growth in the series tier is the
  complexity metric, exposed directly via `accumulator_bitwidth()` and
  register `bit_width()`.

- **JIT specialization.** Runtime-built expression trees are compiled to
  a single ABI-identical `NumVMFn` via partial evaluation (the first
  Futamura projection), with an LLVM ORC backend and a self-contained
  interpreter fallback.

---

## Build Options

| Flag                 | Effect                                                |
|----------------------|-------------------------------------------------------|
| `NAM_USE_LLVM_JIT`   | Use the LLVM ORC `LLJIT` backend in `jit.hpp`.        |
| `NAM_USE_LLVM_APINT` | Replace `BoundedInt` with `llvm::APInt`.              |
| `NAM_USE_GMP`        | Replace vendored `BigInt` with a GMP `mpz_t` wrapper. |
| `NAM_JIT_DEBUG`      | Emit one-line backend/dispatch diagnostics.           |

Requires a **C++20** compiler (Concepts, magic statics, `<bit>`).

---

## Theoretical Lineage

NAM draws on a deep body of work:

- **Computable analysis** — Turing (1936), Weihrauch's Type-2 Theory of
  Effectivity (2000); the undecidability of real equality.
- **Constructive analysis** — Bishop & Bridges (1985); Cauchy sequences
  with explicit modulus of convergence.
- **Exact real arithmetic** — Boehm et al. (1986), Vuillemin (1990),
  Edalat & Potts (1997); reals as digit streams.
- **Coalgebra / corecursion** — Rutten (2000); numbers as final coalgebras
  of streams.
- **p-adic analysis** — Hensel, Koblitz, Gouvêa; Ostrowski's theorem.
- **Partial evaluation** — Futamura projections (1971); Jones, Gomard &
  Sestoft (1993) for the JIT path.

See the per-header documentation and `THEORY.md` for full references.

---

## License

See repository for licensing details.