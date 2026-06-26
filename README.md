# Numbers as Machines — Phase 1 Implementation

A header-mostly C++20 implementation of the **automaton tier**: rationals,
quadratic irrationals, p-adic numbers, base codecs, interval-honest
comparison, the p-adic metric as a product automaton, and periodic-orbit
skip-ahead.

Phase 2 adds the **series tier**: arbitrary-precision accumulators, classical
transcendentals with compiled tail-bound oracles (e, ln 2, 1/e), interval
refinement for online digit extraction, explicit deep-copy fork, and bounded
LRU memoization.

## Build & test

```shell
sudo apt update && sudo apt install -y build-essential ninja-build cmake
rm -rf build && clear && cmake -S . -B build -G Ninja && cmake --build build
ctest --test-dir build --output-on-failure
```

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```
## Diagnosing a hang / debugging tests
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
# Force-flush per line (the harness prints incrementally; ensure no buffering
# when piping through a file or another process):
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

By default the project vendors a tiny bounded-width integer
(`nam::BoundedInt`) so it builds with no external dependencies. To use
`llvm::APInt` instead (as the plan prefers), configure with:

```sh
cmake -S . -B build -G Ninja -DNAM_USE_LLVM_APINT=ON
```

## Milestone mapping

| Milestone         | Header                              | Status                                           |
|-------------------|-------------------------------------|--------------------------------------------------|
| M1 ABI            | `nam/abi.h`, `nam/generator.hpp`    | frozen, `static_assert`ed                        |
| M2 Rationals      | `nam/rational.hpp`                  | constant state, period detection                 |
| M3 Quadratics     | `nam/algebraic.hpp`                 | degree-2 sqrt recurrence, bit-width instrumented |
| M4 Codec          | `nam/codec.hpp`                     | base as projection, round-trips                  |
| M5 p-adics        | `nam/padic.hpp`                     | local commitment, valuation extractor            |
| M6 Compare/Metric | `nam/compare.hpp`, `nam/metric.hpp` | interval-honest, product automaton               |
| M7 Skip           | `nam/skip.hpp`                      | periodic skip + modexp kernel                    |
| P2 BigInt         | `nam/big_int.hpp`                   | vendored arbitrary-precision signed integer      |
| P2 Series         | `nam/series.hpp`, `nam/constants.hpp` | tail-bound oracles, explicit deep-copy fork    |
| P2 Refine         | `nam/refine.hpp`                    | interval-honest online digit extraction          |
| P2 Memo           | `nam/memo.hpp`                      | explicit bounded LRU, no hidden global state     |

## ABI contract (frozen)

- `sizeof(AutomatonVM) == 40` (`4 + 4 + 4*8`).
- Trivially copyable + standard layout: **fork is a literal struct copy**,
  O(1), no hidden state.
- `NAM_ABI_VERSION` bumps on any layout change. Fields are never reordered.

## Honesty commitments preserved

- **No `assert(x == y)`** on values anywhere — equality is undecidable.
  Tests compare bounded digit prefixes; fork determinism is checked **exact**.
- Comparison predicates return tri-state (`Less | Greater | Indistinguishable`)
  or `std::optional<bool>` (pending), never a false definite answer.
- **No global mutable state** below the user layer.
- Memory is the complexity metric: `BoundedInt::bit_width()` growth is
  asserted O(log n) for the n-th digit of a quadratic irrational.
## Series tier (Phase 2) commitments preserved
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
## User tier (Phase 3) commitments preserved
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