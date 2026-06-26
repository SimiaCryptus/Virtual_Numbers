# `nam/generator.hpp` — A C++20 Generator Concept over the NumVMFn ABI

## Overview

This header defines the foundational abstractions for the **automaton tier**
of the NAM (Numeric Automaton Machine) system. It provides three things:

1. **ABI guardrails** — compile-time assertions that pin down the binary
   layout of the core `AutomatonVM` and `NumVMStep` types.
2. **The `Generator` concept** — a C++20 constraint describing any type that
   can advance an automaton state by one digit.
3. **`num_vm_fork`** — an O(1), side-effect-free duplication of automaton
   state, plus a `take` helper for emitting digit streams.

The design is documented in Sections 2.2, 2.3, and 3.1 of the NAM
specification.

---

## Background and Theoretical Context

### Digit streams as state machines

A core idea in this code is that an unbounded sequence of digits — for
example, the decimal expansion of a rational or algebraic number — can be
produced by a **deterministic finite-or-infinite automaton** whose transition
function emits one output symbol per step. This is the classical
*Mealy machine* model (G. H. Mealy, "A Method for Synthesizing Sequential
Circuits," *Bell System Technical Journal*, 1955), in which output is a
function of both the current state and the transition taken.

Here the transition function is captured by the single signature:

```
NumVMStep step(AutomatonVM);
```

where `step` consumes a state and returns *(emitted digit, next state)*. This
is the canonical *unfold* (anamorphism) from functional programming — see
Meijer, Fokkinga & Paterson, "Functional Programming with Bananas, Lenses,
Envelopes and Barbed Wire" (*FPCA*, 1991) — specialized so that the carrier
(the seed) is a fixed-size, trivially-copyable struct.

### Why a fixed-size, trivially-copyable state?

The decision to constrain `AutomatonVM` to exactly 40 bytes
(`4 + 4 + 4*8`), standard-layout, and trivially copyable, is what makes the
*fork* operation O(1) and allocation-free. A trivially copyable type can be
duplicated with the semantics of `std::memcpy` (see [basic.types] in the C++
standard), so forking an automaton's state is a bitwise value copy with no
hidden ownership, no heap traffic, and no aliasing hazards.

This contrasts sharply with the general coroutine/generator machinery in
C++20 (`std::generator`, P2502), whose state lives in a heap-allocated
coroutine frame and therefore cannot be cheaply or safely *copied*. The NAM
automaton tier deliberately sacrifices the syntactic convenience of
`co_yield` to recover the algebraic property that *state is a value* — the
prerequisite for cheap backtracking, speculative evaluation, and parallel
exploration of digit streams.

### Concepts as machine-checked interfaces

The `Generator` concept (C++20, P0734 "Concepts") elevates the Mealy
transition contract into a compile-time predicate. A type satisfies
`Generator` purely by exposing a `static NumVMStep step(AutomatonVM)`. This
is *structural* (duck-typed) rather than *nominal* — no inheritance, no
vtable, and therefore no virtual-dispatch cost across the boundary. Because
`step` is static and intended to be `inline`, the optimizer (Clang in
particular) can inline the transition into the consuming loop, recovering the
performance of a hand-written state machine.

---

## ABI Guardrails (Section 2.2)

The header expresses the binary contract as `static_assert`s, so that any
drift in the struct layout becomes a compile error rather than a runtime
surprise across the C ABI boundary (`nam/abi.h`):

| Assertion                              | Guarantee                                  |
|----------------------------------------|--------------------------------------------|
| `sizeof(AutomatonVM) == 40`            | Layout is `4 + 4 + 4*8` bytes.             |
| `is_trivially_copyable_v<AutomatonVM>` | Fork is a literal struct copy.             |
| `is_standard_layout_v<AutomatonVM>`    | Safe to pass across the C ABI.             |
| `sizeof(NumVMStep) == 48 \|\| <= 56`   | `digit + AutomatonVM`, padding-permitting. |

These are *proofs*, in the spirit of "make illegal states unrepresentable":
the type system enforces the ABI rather than relying on documentation alone.

---

## The `Generator` Concept (Section 3.1)

```cpp
template <typename G>
concept Generator = requires(AutomatonVM s)
{
    { G::step(s) } -> std::same_as<NumVMStep>;
};
```

**Contract.** A `Generator G` must provide a static member
`step` accepting an `AutomatonVM` by value and returning a `NumVMStep`
exactly (enforced by `std::same_as`).

**Design notes.**

- *Static* `step` means there is no per-generator object state — all state is
  threaded explicitly through `AutomatonVM`, preserving referential
  transparency.
- The exact return type (`std::same_as<NumVMStep>`, not merely convertible)
  prevents accidental narrowing or implicit-conversion surprises.

---

## `num_vm_fork` — O(1) State Duplication (Section 2.3)

```cpp
[[nodiscard]] static inline constexpr AutomatonVM num_vm_fork(AutomatonVM s)
{
    return s;
}
```

This is the conceptual centerpiece of Phase 1. Forking an automaton is a pure
value copy:

- **O(1)** — a fixed 40-byte copy, independent of how far the stream has
  advanced.
- **No hidden state** — there is nothing to copy *besides* the struct; the
  `static_assert`s above guarantee this.
- **`constexpr`** — usable at compile time, enabling the entire automaton to
  be evaluated by the compiler when the seed is known.
- **`[[nodiscard]]`** — the result is the only output; discarding it is
  almost certainly a bug.

Because state is a value, forking enables **speculative and branching
evaluation**: a caller may copy a seed, explore one continuation, and discard
or retain it without affecting any other holder of the same seed. This is the
classic persistent-data-structure property, here achieved trivially because
the state is flat and bounded.

---

## `take` — Emitting a Prefix of the Stream

```cpp
template <Generator G, typename It>
constexpr void take(AutomatonVM s, int n, It out)
{
    for (int i = 0; i < n; ++i)
    {
        NumVMStep r = G::step(s);
        *out++ = r.digit;
        s = r.next;
    }
}
```

`take` realizes the *unfold* described above for a finite horizon `n`. It is
a straightforward driver loop that:

1. advances the automaton via `G::step`,
2. writes the emitted digit through the output iterator `out`, and
3. threads the returned `next` state back into the loop.

Constraining the first template parameter to `Generator G` gives clean,
early diagnostics if `G` lacks a conforming `step`. The `It` parameter is an
*output iterator* in the STL sense (see [output.iterators]); any
`std::back_insert_iterator`, raw pointer, or stream iterator works.

Because the loop is `constexpr` and every operation is inlineable and
side-effect-free, the compiler can fully evaluate fixed-`n` digit prefixes at
compile time — for example, materializing the first *k* digits of a constant
into a `std::array` with no runtime cost.

---

## Usage Example

```cpp
 #include "nam/generator.hpp"
 #include <array>

 struct OneThirdGen {
     // Emits the repeating digit 3 of 1/3 = 0.3333...
     static inline constexpr nam::NumVMStep step(nam::AutomatonVM s) {
         return nam::NumVMStep{ /*digit=*/3, /*next=*/s };
     }
 };

 constexpr auto first8 = [] {
     std::array<int, 8> out{};
     nam::take<OneThirdGen>(nam::AutomatonVM{}, 8, out.begin());
     return out;
 }();
 // first8 == {3,3,3,3,3,3,3,3}, computed entirely at compile time.

 // Forking is a value copy:
 nam::AutomatonVM seed{};
 nam::AutomatonVM branch = nam::num_vm_fork(seed); // O(1), independent.
```

---

## References

- G. H. Mealy, *A Method for Synthesizing Sequential Circuits*, Bell System
  Technical Journal, 34(5), 1955. — origin of the output-on-transition
  (Mealy) automaton model.
- E. Meijer, M. Fokkinga, R. Paterson, *Functional Programming with Bananas,
  Lenses, Envelopes and Barbed Wire*, FPCA 1991. — anamorphisms / `unfold`,
  the categorical dual of `fold`.
- ISO/IEC 14882 (C++20), clauses **[basic.types]** (trivially copyable /
  standard-layout semantics) and **[output.iterators]**.
- P0734 *Wording for Concepts* (C++20). — the `concept`/`requires` machinery
  used by `Generator`.
- P2502 / `std::generator`. — the heap-backed coroutine generator this design
  deliberately avoids, in order to keep state copyable.

---

## Summary

`generator.hpp` encodes a Mealy-style digit automaton as a value-semantic,
trivially-copyable state plus a structural `Generator` concept. The ABI
guardrails make the binary layout a compile-time proof; `num_vm_fork` exploits
trivial copyability for genuine O(1), allocation-free forking; and `take`
provides a `constexpr`, fully inlineable driver for emitting finite digit
prefixes. The overarching theme is *state-as-value*: by giving up coroutine
syntax, the automaton tier recovers cheap copying, compile-time evaluation,
and safe speculative branching.