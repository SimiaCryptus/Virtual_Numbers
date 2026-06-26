# Implementation Plan — Phase 1: The Automaton Tier

This document plans the initial phase of development in detail. Phase 1
delivers a working library for the **entire algebraic and p-adic universe**
using only C++20 + Clang `-O3`. No hand-written LLVM IR, no JIT, no GMP.

The guiding constraint from the README and technology stack:

> **Memory is the complexity metric.** Every struct size is a contract.
> The automaton-tier fork is a literal struct copy — O(1), no hidden state.

---

## 1. Goals & Non-Goals

### In scope (Phase 1)

- The `AutomatonVM` ABI struct and `NumVMStep` / `NumVMFn` contract.
- Rationals (constant state, periodic orbit).
- Quadratic irrationals (√2, φ, √3) via degree-2 recurrences.
- p-adic numbers (local digit commitment, periodic automata).
- Base-conversion codecs (base as projection, not baked into the number).
- Matrix-exponentiation **skip-ahead** for periodic/fast-forwardable orbits.
- The p-adic metric as a product automaton.
- Interval-honest comparison predicates for the automaton tier.

### Explicitly deferred

| Deferred to | What                                                                           |
|-------------|--------------------------------------------------------------------------------|
| Phase 2     | Series tier, transcendentals, tail-bound oracles, GMP, signed-digit arithmetic |
| Phase 3     | Python/pybind11 user layer, precision contexts                                 |
| Phase 4     | ORC v2 JIT, `IRBuilder`, expression-tree compilation, SIMD batching            |

---

## 2. The ABI Boundary (do this first)

Everything depends on freezing the C-compatible POD structs. Once written,
these must be **versioned and never silently reordered**.

### 2.1 Core structs

```c
// include/nam/abi.h  --  extern "C", plain C POD, no C++ above this line
typedef struct {
    uint32_t base;        // codec selector — NOT baked into the number
    uint32_t phase;       // periodic-orbit phase / index
    uint64_t state[4];    // over-provisioned for degree-4 algebraic
} AutomatonVM;

typedef struct {
    uint32_t   digit;
    AutomatonVM next;
} NumVMStep;

typedef NumVMStep (*NumVMFn)(AutomatonVM);
```

### 2.2 ABI guardrails (write the tests before the code)

- `static_assert(sizeof(AutomatonVM) == 40)` — `4 + 4 + 4*8`.
- `static_assert(std::is_trivially_copyable_v<AutomatonVM>)` — proves fork
  is a struct copy.
- `static_assert(std::is_standard_layout_v<AutomatonVM>)` — C-ABI safe.
- A `clang-tidy` rule banning global mutable state below the user layer.

### 2.3 Fork (the whole point of Phase 1)

```cpp
static inline AutomatonVM num_vm_fork(AutomatonVM s) {
    return s;  // pure value copy — O(1), no hidden state
}
```

The README's "forking is a struct copy" claim is **fully and without
qualification** true here. Tests must prove fork determinism: fork, then
compare digit prefixes — must be *exact*, not interval-honest.

---

## 3. Module Breakdown

```
include/nam/
  abi.h              # C POD structs, NumVMFn typedef  (Section 2)
  generator.hpp      # C++ Generator concept over NumVMFn
  rational.hpp       # constant-state periodic VM
  algebraic.hpp      # degree-2 recurrence VM (quadratic irrationals)
  padic.hpp          # p-way MUX, local commitment, valuation
  codec.hpp          # base projection / base-conversion wrapper VM
  skip.hpp           # matrix-exponentiation fast-forward
  compare.hpp        # definitely_less_than / agrees_with / compare(Trit)
  metric.hpp         # p-adic distance as product automaton
src/
  *.cpp              # only where templates can't stay header-only
tests/
  abi_test.cpp
  rational_test.cpp
  algebraic_test.cpp
  padic_test.cpp
  codec_test.cpp
  skip_test.cpp
  property_test.cpp  # rapidcheck algebraic laws
```

### 3.1 The `Generator` concept

Use a C++20 concept (not SFINAE) to constrain anything that implements
`step`. The static path relies on Clang inlining across this boundary, so
keep step functions `inline`/`static` and `constexpr`-clean.

```cpp
template <typename G>
concept Generator = requires(G g, AutomatonVM s) {
    { G::step(s) } -> std::same_as<NumVMStep>;
};
```

### 3.2 Rationals — constant state

A rational `p/q` in base `b` is a constant-state VM: one phase variable and
a fixed payload. The struct does **not** grow as digits are emitted. The
digit step is long-division-by-multiplication: `state[0]` holds the running
remainder, `phase` cycles through the (provably finite) period.

- `step`: `r = r * b; digit = r / q; r = r % q;`
- Period detection: track remainder; a repeat closes the orbit (enables
  skip-ahead trivially).

### 3.3 Quadratic irrationals — degree-2 recurrence

√2, φ, √3 are algebraic of degree 2 → **two coupled registers**. The struct
over-provisions to `state[4]`, but the *logical* register count is 2 (this
distinction matters for the complexity-metric instrumentation, see §6).

- Bit-width of the registers grows `O(log n)` for the n-th digit — track and
  assert this growth in tests, since memory is the metric.
- Use `APInt` (already a dependency via LLVM) for bounded bit-growth, **not**
  GMP. GMP is a Phase-2 concern only.

### 3.4 p-adics — local commitment is the easy win

p-adics are **easier than the reals**: digit commitment is *local*. Build
these as a p-way MUX indexed from the least-significant digit upward.

- A p-adic rational has an ultimately periodic expansion → finite state space
  with a periodic orbit → skip-ahead for free.
- `valuation(a_n) → v_p(a_n)` extractor: a term with valuation `v` can only
  affect digits at positions `≤ v`. No tail-bound oracle, no interval
  refinement, no boundary pathologies — that is *all* deferred to Phase 2.

### 3.5 Codec layer — base as projection

The base lives in the `base` field, not in the number. Changing base changes
the projection, not the number. Implement base conversion as a wrapper VM
that re-decodes the underlying generator's digit stream into the target
alphabet. The ABI does **not** change when codecs are swapped.

### 3.6 Skip-ahead — matrix exponentiation

For periodic / fast-forwardable orbits, expose:

```c
AutomatonVM skip(uint64_t n, AutomatonVM s);
```

- For genuine periodic orbits (rationals, periodic p-adics): jump via
  `phase = (phase + n) % period`.
- For the general fast-forwardable structure: repeated squaring of the
  transition matrix modulo the relevant quantity → O(log n).
- This is the computational form of a BBP-type formula. It is **not**
  synthesized automatically; per-constant derivation is required. Phase 1
  only needs the periodic-orbit cases (rationals + p-adic rationals); the
  π-base-16 case can be added as a worked example but is optional here.

### 3.7 Comparison & metric — interval-honest

Even in the automaton tier, equality is undecidable in general. Ship the
honest predicates only:

```cpp
enum class Trit { Less, Greater, Indistinguishable };
bool definitely_less_than(AutomatonVM x, AutomatonVM y);   // true/false/pending
bool agrees_with(AutomatonVM x, AutomatonVM y, int digits);
Trit compare(AutomatonVM x, AutomatonVM y, int max_digits);
```

The p-adic metric `p^{-v_p(x-y)}` is a **product automaton** over the two
VMs' state spaces — itself finite and periodic when both inputs are periodic.

---

## 4. Build & Tooling Setup

Mirrors the technology stack's "Phase 1: Pure C++20 + Clang `-O3`, `APInt`,
no JIT, no GMP."

- **CMake ≥ 3.20** with `find_package(LLVM CONFIG)` (needed only for `APInt`
  / `llvm/ADT`, not for codegen yet).
- **Ninja** generator.
- **Clang 17/18**, build with `-O2`/`-O3` so inlining across the generator
  graph actually happens.
- **clang-format** + **clang-tidy** (with the no-global-mutable-state check).
- **Sanitizers**: ASan + UBSan on the test targets — essential given the raw
  POD ABI.

### Minimal CMake skeleton

```cmake
cmake_minimum_required(VERSION 3.20)
project(numbers_as_machines CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(LLVM CONFIG REQUIRED)   # for llvm::APInt only in Phase 1
add_library(nam INTERFACE)           # header-only where possible
target_include_directories(nam INTERFACE include)
enable_testing()
add_subdirectory(tests)
```

---

## 5. Testing Strategy (interval-honest from day one)

Equality is undecidable, so `assert(x == y)` is forbidden. Test
infrastructure must be interval-honest from the start.

- **Catch2 v3** or **GoogleTest** for unit structure.
- **rapidcheck** property tests:
    - commutativity: `agrees_with(x + y, y + x, N)` for random `N`,
    - codec round-trips: `in_base(b)` then reproject agrees to `N` digits,
    - fork determinism: `fork()` then compare prefixes — must be **exact**.
- **Golden digit streams** for known constants:
    - √2, φ in base 10 against published digits,
    - p-adic expansions of small rationals against hand computation.
- **Memory instrumentation**:
    - `static_assert(sizeof(AutomatonVM) == 40)`,
    - assert logical register counts match the complexity table
      (rationals = 1, quadratic = 2),
    - track `APInt` bit-width growth is `O(log n)`.

---

## 6. Complexity-Metric Instrumentation

Because *memory is the complexity metric*, Phase 1 must measure it, not just
assert struct sizes. For each generator class, record the **logical** minimal
register count (not the padded `state[4]` width):

| Class                 | Logical registers | Bit-width growth |
|-----------------------|-------------------|------------------|
| Rationals             | 1                 | constant         |
| Quadratic irrationals | 2                 | O(log n)         |
| p-adic rationals      | 1 (periodic)      | constant         |

Add a Google Benchmark target (deferred-but-stubbed) that will, in later
phases, track live `ArbitraryInt` bytes. In Phase 1 it tracks `APInt`
bit-width vs. digit index and asserts the growth shape.

---

## 7. Milestones & Sequencing

1. **M1 — ABI frozen.** `abi.h`, all `static_assert`s green, fork-determinism
   test passing. *Nothing proceeds until this is locked.*
2. **M2 — Rationals.** Constant-state VM, period detection, trivial skip.
3. **M3 — Quadratic irrationals.** Degree-2 recurrence, `APInt` bit-growth
   instrumented, golden-stream tests for √2 / φ.
4. **M4 — Codec layer.** Base projection wrapper, round-trip property tests.
5. **M5 — p-adics.** p-way MUX, valuation extractor, local commitment.
6. **M6 — Comparison + p-adic metric.** Interval-honest predicates, product
   automaton for distance.
7. **M7 — Skip-ahead (periodic cases).** Matrix-exponentiation for
   rationals/p-adic rationals; optional π-base-16 worked example.

Each milestone is independently shippable. Per the README roadmap, no phase
is wasted if a later one is delayed — and inside Phase 1, no milestone is
wasted if a later milestone slips.

---

## 8. Risks & Watch-Outs

- **ABI drift.** The C structs are a contract with future JIT-emitted code
  (Phase 4). Version them now; never reorder fields silently.
- **Hidden global state.** Forbidden below the user layer — it breaks
  value-semantics forking. Enforce via clang-tidy + review.
- **`constexpr` discipline.** Keep the automaton tier `constexpr`-clean; this
  pays off when static generator trees get folded by Clang. (GMP/LLVM runtime
  types that break `constexpr` are a Phase-2+ concern.)
- **Exact-boundary honesty.** Even here, do not pretend exact equality is
  decidable. Predicates return tri-state / pending, never a false `true`.

---

## 9. Definition of Done (Phase 1)

- All seven milestones green.
- A working, header-mostly C++20 library covering rationals, quadratic
  irrationals, and p-adic numbers under arbitrary base codecs.
- O(1) struct-copy fork, proven by determinism tests.
- Interval-honest comparison and a periodic-orbit skip primitive.
- Memory/complexity instrumentation matching the README's complexity table
  for every implemented class.
- The ABI frozen and versioned, ready for Phases 2–4 to build on without
  breaking the C calling convention.