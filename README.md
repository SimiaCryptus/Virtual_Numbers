# Numbers as Machines

A header-mostly C++20 library that models numbers as **machines that emit
digits on demand** rather than as fixed-precision floats. A number is a tiny
forkable virtual machine: you ask it for the next digit, fork it in O(1) (or
O(log n) for the series tier), reproject it into a new base, compare it
against another machine — all while staying *interval-honest* (equality is
undecidable, so the library never returns a false definite answer).

The design is organized into two tiers and four implementation phases:

- **Automaton tier** — constant- or bounded-state machines whose fork is a
  literal struct copy: rationals, quadratic irrationals, p-adic numbers.
- **Series tier** — growing arbitrary-precision accumulators for classical
  transcendentals (e, ln 2, 1/e) backed by compiled tail-bound oracles.

---

## Table of contents

- [Concepts](#concepts)
- [Quick start](#quick-start)
- [Build & test](#build--test)
- [The four phases](#the-four-phases)
- [JIT compilation path (Phase 4)](#jit-compilation-path-phase-4)
- [Python bindings (Phase 3 user layer)](#python-bindings-phase-3-user-layer)
- [Debugging a hang](#debugging-a-hang)
- [Header / milestone map](#header--milestone-map)
- [The frozen ABI contract](#the-frozen-abi-contract)
- [Honesty commitments](#honesty-commitments)

---

## Concepts

Every number is a state machine exposing a single step:

```text
NumVMStep step(AutomatonVM)   // -> { uint32_t digit, AutomatonVM next }
```

Two consequences fall out of this shape:

1. **Fork is value-copy.** For the automaton tier the entire state is a
   40-byte POD (`AutomatonVM`), so forking a number is a `memcpy` — O(1),
   no hidden state, no copy-on-write surprises. For the series tier the
   accumulators are arbitrary-precision integers, so fork is an *explicit*
   deep copy, advertised as O(log n) in the computation depth.

2. **Memory is the complexity metric.** A rational stays at constant state.
   A degree-2 algebraic number's accumulated-root register grows O(log n)
   in the digit index. A transcendental's accumulators grow with depth.
   The library instruments `bit_width()` so tests can assert these shapes.

Comparison and metrics are **interval-honest**: predicates return tri-state
(`Less | Greater | Indistinguishable`) or `std::optional` (pending), never a
fabricated definite answer for values that cannot be distinguished within
the requested precision.

 ---

## Quick start

 ```cpp
 #include "nam/number.hpp"
 using namespace nam;

 int main() {
     // 1/7 = 0.(142857) — emit twelve digits.
     Number r = Number::rational(1, 7, 10);
     auto ds = r.digits(12);                 // {1,4,2,8,5,7,1,4,2,8,5,7}

     // Base is a codec, not part of the value: 1/4 in base 2 is 0.01.
     Number quarter = Number::rational(1, 4, 10);
     quarter.in_base(2).digits(4);           // {0,1,0,0}

     // Fork is O(1) and exact for the automaton tier.
     auto [a, b] = r.fork();
     bool same = (a.digits(20) == b.digits(20));   // true

     // Transcendentals stream from the series tier (1/e = 0.36787944...).
     Number inv_e = Number::one_over_e(10);
     inv_e.digits(6);                        // {3,6,7,8,7,9}

     // Interval-honest comparison: 1/7 < 1/3.
     Number third = Number::rational(1, 3, 10);
     r.compare(third, 5);                    // Trit::Less
 }
 ```

 ---

## Build & test

 ```sh
 # Dependencies (Debian/Ubuntu). Only build tooling is required for the core;
 # the zstd/zlib/edit/curl packages are for the optional LLVM JIT backend.
 sudo apt update && sudo apt install -y \
     build-essential ninja-build cmake \
     libzstd-dev zlib1g-dev libedit-dev libcurl4-openssl-dev

 rm -rf build && cmake -S . -B build -G Ninja && cmake --build build
 ctest --test-dir build --output-on-failure
 ```

The core library has **zero external dependencies** by default: arbitrary
precision is provided by a vendored `nam::BigInt`, bounded integers by
`nam::BoundedInt`, and the JIT by a function-pointer interpreter. Optional
upgrades are gated behind CMake flags:

| Flag                      | Effect                                           | Default |
 |---------------------------|--------------------------------------------------|---------|
| `-DNAM_USE_LLVM_APINT=ON` | Use `llvm::APInt` for bounded integers           | off     |
| `-DNAM_USE_GMP=ON`        | Use GMP `mpz_t` for the series tier accumulators | off     |
| `-DNAM_USE_LLVM_JIT=ON`   | Real LLVM ORC v2 JIT backend (vs interpreter)    | off     |
| `-DNAM_BUILD_PYTHON=ON`   | Build the pybind11 user-layer bindings           | off     |
| `-DNAM_SANITIZE=ON`       | ASan + UBSan on the test target                  | **on**  |

 ---

## The four phases

| Phase | Theme                 | What it adds                                                                                      |
 |-------|-----------------------|---------------------------------------------------------------------------------------------------|
| **1** | Automaton tier        | Frozen ABI, rationals, quadratic irrationals, base codecs, p-adics, comparison/metric, skip-ahead |
| **2** | Series tier           | Vendored `BigInt`, transcendentals with tail-bound oracles, interval refinement, bounded LRU memo |
| **3** | User-facing API       | `Number` type: operator surface, precision contexts, explicit memoization, honest comparisons     |
| **4** | JIT / expression tree | `compile(expr_tree) -> NumVMFn` for runtime-built trees, sharing the static C ABI exactly         |

### Phase 1 — automaton tier

A rational `p/q` in base `b` is a constant-state machine: the only mutable
state is the running remainder, advanced by long-division-by-multiplication.
Quadratic irrationals (`sqrt(D)`) use the classic digit-by-digit long-hand
square-root recurrence — genuinely two registers (remainder + root prefix),
with the root prefix's bit-width growing O(log n). p-adic rationals commit
digits *locally* from the least-significant end, giving ultimately periodic
expansions and therefore free skip-ahead.

### Phase 2 — series tier

Transcendentals are convergent series carrying an attached **tail-bound
oracle** (a compiled convergence proof). The value is tracked as an exact
rational `num/den` plus an error numerator; a digit is committed only when
the interval `[num/den, (num+err)/den]` is narrow enough that its leading
digit is unambiguous. Refinement pulls more terms until the digit commits or
a budget is exhausted (honest pending).

### Phase 3 — user-facing API

The `Number` type is a thin tagged union over the two tiers. It hides the raw
ABI behind an `mpmath`/`Decimal`-flavoured surface: `digits(n)`, `in_base(b)`,
`fork()`, `skip(n)`, `compare(...)`, `to_string(...)`, scoped
`precision_context(digits=N)`, and explicit `.streaming()` / `.cached(N)`
memoization modes.

### Phase 4 — JIT / expression tree

See [JIT compilation path](#jit-compilation-path-phase-4).

 ---

## JIT compilation path (Phase 4)

Runtime-constructed expression trees (`nam::Expr`) are compiled to a single
specialized `NumVMFn` via `nam::compile(expr_tree)` (THEORY.md "LLVM as the
Execution Substrate"). The returned `CompiledFn::fn()` has the *exact* same
C ABI as the static path — `NumVMStep (*)(AutomatonVM)` — so every existing
combinator (compare, metric, skip, codec) consumes it unchanged.

By default the JIT is **off** and `compile()` binds a function-pointer
*interpreter* trampoline honoring the same ABI, so the core builds with zero
external dependencies. Enable the real LLVM ORC v2 backend (emits IR via
`IRBuilder`, materializes through `LLJIT`) with:

```sh
cmake -S . -B build -G Ninja -DNAM_USE_LLVM_JIT=ON
cmake --build build && ctest --test-dir build --output-on-failure
```

`CompiledFn::is_native()` reports whether a real JIT (vs the interpreter)
produced the function. Both paths reproduce the static digit stream exactly.

Example:

```cpp
auto e = Expr::rebase(Expr::leaf_rational(1, 4, 10), 2); // 1/4 in base 2
CompiledFn cf = compile(*e);
AutomatonVM s = resolve_expr(*e).seed;
NumVMStep r = cf.step(s);   // r.digit == 0, then 1, 0, 0 ...
```

---

## Python bindings (Phase 3 user layer)

The ergonomic user layer is exposed to Python via pybind11. It is off by
default so the core still builds with no external dependencies; enable it
with `-DNAM_BUILD_PYTHON=ON` (pybind11 is located via `find_package`,
falling back to `FetchContent`):

```sh
cmake -S . -B build -G Ninja -DNAM_BUILD_PYTHON=ON
cmake --build build
PYTHONPATH=build/bindings python3 bindings/example.py
PYTHONPATH=build/bindings python3 bindings/test_nam_py.py
```

The honesty commitments cross the language boundary unchanged: comparison
maps to Python tri-state (`True | False | None`), fork cost is annotated by
tier (`fork_cost()`), memoization is an explicit mode (`.streaming()` /
`.cached(N)`), and `precision_context(digits=N)` is a scoped context manager.

---

## Debugging a hang

`ctest` buffers a test's stdout and only prints it **after** the test
finishes. If a test hangs (e.g. an infinite loop in digit generation), you
see only `Start 1: nam_tests` and nothing else. To diagnose, bypass ctest
and run the binary directly so the per-test `[ RUN ]` / `[ OK ]` lines stream
live — the last `[ RUN ]` printed is the test that is hanging:

```sh
./build/tests/nam_tests
```

Useful variants:

```sh
# Stream ctest output live instead of buffering (ctest >= 3.17), and add a
# hard timeout so a hang fails fast instead of blocking CI forever.
ctest --test-dir build --output-on-failure \
   --output-junit results.xml \
   --test-output-size-passed 0 \
   --timeout 30 -V

# Force-flush per line when piping through a file or another process:
stdbuf -oL -eL ./build/tests/nam_tests
```

### Pinpoint the hanging line with a debugger

Run under `gdb`, let it hang, then interrupt with `Ctrl-C` and inspect the
stack — the top frames reveal exactly which generator/extractor is looping:

```sh
gdb ./build/tests/nam_tests
(gdb) run
# ... wait for the hang, then press Ctrl-C ...
(gdb) bt          # backtrace of the stuck thread
(gdb) frame N     # hop to the NAM frame of interest
(gdb) info locals # inspect loop counters / interval bounds
```

Or attach to the already-running process:

```sh
./build/tests/nam_tests &
gdb -p $(pgrep -n nam_tests)
(gdb) bt
```

### Sanitizers and timeouts

Tests are built with ASan + UBSan by default (`-DNAM_SANITIZE=ON`), which
catches the *non-hanging* failure modes (UB, OOB, leaks). A hang is not a
sanitizer event, so combine sanitizers with an external watchdog:

```sh
timeout 30 ./build/tests/nam_tests || echo "hung or failed (exit $?)"
```

`timeout` exits `124` on a hang, giving CI a clean signal instead of an
indefinite block.

### Building a debug binary for clearer backtraces

```sh
cmake -S . -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug
gdb ./build-debug/tests/nam_tests
```

---

## Header / milestone map

| Milestone         | Header                                | Status                                           |
 |-------------------|---------------------------------------|--------------------------------------------------|
| M1 ABI            | `nam/abi.h`, `nam/generator.hpp`      | frozen, `static_assert`ed                        |
| M2 Rationals      | `nam/rational.hpp`                    | constant state, period detection                 |
| M3 Quadratics     | `nam/algebraic.hpp`                   | degree-2 sqrt recurrence, bit-width instrumented |
| M4 Codec          | `nam/codec.hpp`                       | base as projection, round-trips                  |
| M5 p-adics        | `nam/padic.hpp`                       | local commitment, valuation extractor            |
| M6 Compare/Metric | `nam/compare.hpp`, `nam/metric.hpp`   | interval-honest, product automaton               |
| M7 Skip           | `nam/skip.hpp`                        | periodic skip + modexp kernel                    |
| P2 BigInt         | `nam/big_int.hpp`                     | vendored arbitrary-precision signed integer      |
| P2 Series         | `nam/series.hpp`, `nam/constants.hpp` | tail-bound oracles, explicit deep-copy fork      |
| P2 Refine         | `nam/refine.hpp`                      | interval-honest online digit extraction          |
| P2 Memo           | `nam/memo.hpp`                        | explicit bounded LRU, no hidden global state     |
| P3 User API       | `nam/number.hpp`                      | `Number` type, precision contexts, fork cost     |
| P4 JIT / Expr     | `nam/expr.hpp`, `nam/jit.hpp`         | `compile(expr) -> NumVMFn`, interp + LLVM paths  |

 ---

## The frozen ABI contract

The automaton tier speaks a versioned, C-compatible ABI (`nam/abi.h`):

 ```c
 typedef struct {
     uint32_t base;       /* codec selector — NOT baked into the number */
     uint32_t phase;      /* periodic-orbit phase / digit index          */
     uint64_t state[4];   /* over-provisioned for degree-4 algebraic     */
 } AutomatonVM;           /* sizeof == 40                                */

 typedef struct { uint32_t digit; AutomatonVM next; } NumVMStep;
 typedef NumVMStep (*NumVMFn)(AutomatonVM);
 ```

- `sizeof(AutomatonVM) == 40` (`4 + 4 + 4*8`) — checked by `static_assert`.
- Trivially copyable + standard layout: **fork is a literal struct copy**,
  O(1), no hidden state.
- `NAM_ABI_VERSION` bumps on any layout change. Fields are never reordered.

---

## Honesty commitments

These hold across every tier and language binding.

**Core (Phase 1)**

- **No `assert(x == y)`** on values anywhere — equality is undecidable.
  Tests compare bounded digit prefixes; fork determinism is checked **exact**.
- Comparison predicates return tri-state (`Less | Greater | Indistinguishable`)
  or `std::optional<bool>` (pending), never a false definite answer.
- **No global mutable state** below the user layer.
- Memory is the complexity metric: `BoundedInt::bit_width()` growth is
  asserted O(log n) for the n-th digit of a quadratic irrational.

**Series tier (Phase 2)**

- **Fork is an explicit deep copy** of the `BigInt` accumulators —
  `SeriesVM::fork()` is O(size) in computation depth, never copy-on-write.
  `series_fork_is_deep_copy` proves the fork is unaffected by later mutation.
- **Every constant ships a compiled convergence proof**: the tail-bound
  oracle is part of the `SeriesSpec`, so digits are only committed when the
  interval honestly distinguishes them.
- **Digit extraction is interval-honest**: `next_digit` returns
  `std::optional` — a boundary value that cannot be distinguished yields a
  pending (`nullopt`), never a false definite digit.
- **Memoization is explicit and bounded**: `LruDigitCache` /
  `CachedDigitSource` are opt-in wrappers; there is no hidden global cache.

**User tier (Phase 3)**

- **Precision contexts are scoped, not global**: `PrecisionContext` is a
  thread-local RAII guard. Nesting restores the prior value on scope exit;
  there is no global mutable precision leaking across threads or scopes.
- **Fork cost is annotated by tier**: `Number::fork_cost()` reports `O(1)`
  for the automaton tier and `O(log n)` for the series tier, and the fork
  itself dispatches to the matching copy (struct copy vs deep copy).
- **Comparison stays interval-honest at the user layer**: `definitely_less_than`
  returns `std::optional<bool>` (pending) and `compare` returns a `Trit`;
  `agrees_with` is exact for a finite prefix. No false definite equality.
- **Memoization is an explicit mode**: `.streaming()` (O(1) space) and
  `.cached(max_digits=N)` (bounded LRU) are user choices; forks receive
  independent caches so value semantics are never violated.
- **Base is a codec**: `in_base(b)` reprojects without mutating the original
  Number (`base()` of the source is unchanged).

**JIT tier (Phase 4)**

- **The compiled function is a real, C-callable `NumVMFn`** with the exact
  static ABI; the interpreter's dispatch table lives honestly *behind* the
  function pointer rather than corrupting any ABI field.
- Both the interpreter and LLVM backends **reproduce the static digit stream
  exactly** — verified digit-for-digit in the Phase 4 tests.