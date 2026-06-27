# Technology Stack

This document lays out the full technology stack for **Numbers as Machines**,
with notes oriented toward an experienced developer who is new to LLVM and a
bit dated on modern C++.

---

## TL;DR

| Layer         | Choice                                   | Why                                                                 |
|---------------|------------------------------------------|---------------------------------------------------------------------|
| Core language | **C++20** (LLVM/Clang)                   | LLVM is itself C++; ABI control; zero-overhead abstractions         |
| Codegen / JIT | **LLVM 17+ (ORC v2 JIT)**                | Inline generator graphs, JIT runtime expression trees               |
| Big integers  | **GMP** (or libtommath)                  | `ArbitraryInt` accumulators for the series tier                     |
| Build         | **CMake + Ninja**                        | Standard for LLVM-adjacent projects                                 |
| Bindings      | **pybind11**                             | The `mpmath`-style Python user layer                                |
| Testing       | **Catch2 / GoogleTest** + property tests | Digit-stream equivalence is interval-honest, needs property testing |
| Benchmark     | **Google Benchmark**                     | Memory-as-complexity-metric instrumentation                         |

---

## Why C++ (and which C++)

LLVM is written in C++ and its primary, best-supported API is its C++ API.
Building a tool whose core value proposition is *"LLVM compiles the generator
graph for you"* makes C++ the path of least resistance. You stay in one
language from ABI struct definitions through to IR generation.

**Target C++20.**:

- **`std::span`** — non-owning views over digit buffers without pointer+length pairs.
- **Concepts** — constrain the `Generator` interface at compile time instead of SFINAE.
- **`[[no_unique_address]]`** — keeps the `AutomatonVM` struct tight (matters: memory *is* the complexity metric).
- **Designated initializers** — readable POD initialization for the ABI structs.
- **`constexpr` everything** — compile-time evaluation of static generator trees.
- **`std::bit_cast`** — safe reinterpretation for the codec/base projection layer.
- **Modules** — optional; LLVM tooling support is still maturing, so prefer headers initially.

The ABI structs in the README (`AutomatonVM`, `SeriesVM`, `NumVMStep`) are
deliberately **C-compatible POD** (`extern "C"` linkage). This is intentional:
the JIT-emitted functions must match a stable C calling convention. Keep the
boundary types in plain C; use C++ only above the ABI line.

---

## Arbitrary-Precision Integers

The **series tier** needs growing accumulators (`ArbitraryInt`):

> `ArbitraryInt *accum;  // mutable, deep-copy on fork`

Options:

- **GMP (libgmp)** — fastest, battle-tested, LGPL. Recommended default.
  Wrap with a thin RAII type so the `O(log n)` deep-copy-on-fork cost is explicit.
- **libtommath** — simpler license (public domain), slower, easier to embed.
- **LLVM's own `APInt`** — *fixed* width, good for the automaton tier's
  bounded bit-growth, but **not** for unbounded series accumulators.

**Recommendation:** `APInt` for automaton-tier bit-width tracking (it's already
a dependency via LLVM), GMP for the series-tier unbounded accumulators.

> ⚠️ The deep-copy-on-fork cost is part of the public contract. Do not hide it
> behind copy-on-write that silently amortizes — the README's honesty about
> `O(log n)` fork cost is a design commitment, not an implementation detail.

---

## Memoization & Caching

The README mandates **explicit, bounded LRU caches sized to L2/L3** and forbids
hidden global state.

- Roll a small intrusive LRU (don't pull in a heavy cache library).
- Size it from `std::hardware_destructive_interference_size` plus a runtime
  cache-size probe, or just expose `max_digits` as the README API shows.
- **No `static`/global mutable state** anywhere below the user layer — it
  breaks value-semantics forking. Enforce this in code review and with a
  clang-tidy check for global mutable state.

---

## Build System & Tooling

- **CMake (≥ 3.20)** with `find_package(LLVM CONFIG)`. This is the canonical
  way to consume LLVM; it gives you the right include dirs, defines, and the
  `llvm_map_components_to_libnames` helper.
- **Ninja** as the generator (LLVM builds are large; Ninja's incrementality matters).
- **clang-format** + **clang-tidy** — you already have Clang as a dep; use it.
- **sanitizers** — ASan/UBSan are essential given the raw POD ABI and manual
  `ArbitraryInt` lifetime management. MSan for the JIT-emitted code paths.

---

## Language Bindings (User Layer)

The README's user API is Python and reads like `mpmath`/`Decimal`:

```python
with precision_context(digits=50):
    result = sin(pi / 4) + sqrt(2)
```

- **pybind11** — header-only, modern, integrates with CMake cleanly. Use this
  for the user layer (operator overloading, precision contexts, fork).
- Expose the *combinator layer*, not the raw `NumVMFn` ABI, to Python.
- The honest comparison predicates (`agrees_with`, `definitely_less_than`)
  map to Python tri-state returns (`True | False | None`).

---

## Testing Strategy

Equality is **undecidable** (Rice's theorem, per the README), so conventional
`assert(x == y)` is impossible. Test infrastructure must be interval-honest:

- **Catch2** or **GoogleTest** for unit structure.
- **Property-based testing** (rapidcheck) for algebraic laws:
    - `agrees_with(x + y, y + x, digits=N)` for random N,
    - codec round-trips (`in_base(b)` then reproject),
    - fork determinism (`fork()` then compare prefixes — must be exact).
- **Golden digit streams** for known constants (π in base 16 via BBP `skip`,
  compared against published hex digits).
- **Memory instrumentation** — assert struct sizes (`static_assert(sizeof(AutomatonVM) == ...)`)
  since memory *is* the complexity metric. Track live `ArbitraryInt` bytes in series-tier tests.

---

## Recommended Dependency Versions (pin these)

| Dependency       | Version      | Notes                                          |
|------------------|--------------|------------------------------------------------|
| LLVM / Clang     | 17.x or 18.x | ORC v2, opaque pointers default                |
| C++ standard     | C++20        | Concepts, `std::span`, `[[no_unique_address]]` |
| CMake            | ≥ 3.20       | `find_package(LLVM CONFIG)`                    |
| GMP              | ≥ 6.2        | series-tier accumulators                       |
| pybind11         | ≥ 2.11       | Python user layer                              |
| Catch2           | v3           | testing                                        |
| Google Benchmark | latest       | memory/perf instrumentation                    |

---

## Phased Stack Adoption (mirrors the README roadmap)

- **Phase 1 (Automaton tier):** Pure C++20 + Clang `-O3`. `APInt` for bit-width.
  *No JIT, no GMP needed.* This is where you build C++ fluency back up.
- **Phase 2 (Series tier):** Add GMP, signed-digit arithmetic, LRU memo.
  Still no LLVM IR by hand — Clang static optimization only.
- **Phase 3 (User API):** Add pybind11 and the ergonomic Python layer.
- **Phase 4 (JIT):** *Now* learn ORC v2 / `IRBuilder`. By this point the ABI
  is frozen and you only need LLVM to emit the same struct/step contract.

> Crucially: **you can defer all hand-written LLVM IR to Phase 4.** The static
> path (Phases 1–3) only requires *using* Clang as an optimizing compiler, not
> *programming* LLVM. This lets you ship a real library before you become an
> LLVM expert.

---

## Things to Watch Out For

- **ABI stability:** the C structs are a contract between hand-written C++ and
  JIT-emitted code. Version them; never reorder fields silently.
- **Opaque pointers:** LLVM 15+ defaults to opaque pointers — older tutorials
  using `getPointerElementType()` are stale. Use LLVM 17/18 docs only.
- **GMP exceptions vs. C ABI:** GMP is a C library; keep its calls behind your
  RAII wrapper and never let a C++ exception unwind through a JIT-emitted
  C-ABI frame.
- **`constexpr` limits:** the static generator graph wants `constexpr`, but
  GMP and LLVM types are not `constexpr`. Keep the automaton tier `constexpr`-clean
  and accept that the series tier is runtime-only.