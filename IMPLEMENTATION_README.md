# Numbers as Machines — Phase 1 Implementation

A header-mostly C++20 implementation of the **automaton tier**: rationals,
quadratic irrationals, p-adic numbers, base codecs, interval-honest
comparison, the p-adic metric as a product automaton, and periodic-orbit
skip-ahead.

## Build & test

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

By default the project vendors a tiny bounded-width integer
(`nam::BoundedInt`) so it builds with no external dependencies. To use
`llvm::APInt` instead (as the plan prefers), configure with:

```sh
cmake -S . -B build -G Ninja -DNAM_USE_LLVM_APINT=ON
```

## Milestone mapping

| Milestone | Header                | Status |
|-----------|-----------------------|--------|
| M1 ABI    | `nam/abi.h`, `nam/generator.hpp` | frozen, `static_assert`ed |
| M2 Rationals | `nam/rational.hpp` | constant state, period detection |
| M3 Quadratics | `nam/algebraic.hpp` | degree-2 sqrt recurrence, bit-width instrumented |
| M4 Codec | `nam/codec.hpp` | base as projection, round-trips |
| M5 p-adics | `nam/padic.hpp` | local commitment, valuation extractor |
| M6 Compare/Metric | `nam/compare.hpp`, `nam/metric.hpp` | interval-honest, product automaton |
| M7 Skip | `nam/skip.hpp` | periodic skip + modexp kernel |

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