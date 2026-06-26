# `nam/jit.hpp` — Runtime Specialization of Expression Trees

## Overview

`jit.hpp` implements **Phase 4** of the NAM pipeline: the transformation
of a runtime-constructed expression tree into a single, specialized
step function (`NumVMFn`). It is the concrete realization of the design
principle stated in `THEORY.md` under *"LLVM as the Execution
Substrate"*: expression trees that are only known at runtime cannot be
optimized by ahead-of-time compilation alone, so we defer final code
generation to the moment the tree is fixed.

The single public entry point is:

```cpp
    CompiledFn compile(const Expr& expr);
```

which returns a move-only handle exposing a raw, C-callable function
pointer with **exactly** the static-path ABI:

```cpp
    NumVMStep (*)(AutomatonVM)   // == NumVMFn
```

Because the returned pointer is ABI-identical to the statically linked
step functions, every downstream consumer (`compare`, `metric`, `skip`,
`codec`) operates on JIT- and statically-produced functions
interchangeably.

---

## Background and Theory

### Just-in-time specialization

The technique here is a textbook instance of **partial evaluation**
(Futamura, *"Partial Evaluation of Computation Process"*, 1971): given a
general interpreter `interp(program, data)` and a fixed `program`, one
can mechanically derive a specialized residual program
`interp_program(data)` that runs faster because the program structure is
"baked in." Here the *program* is the expression tree, and the *residual
program* is the emitted `NumVMFn`. The first Futamura projection —
specializing an interpreter with respect to a source program to obtain a
compiled target — is precisely what `compile()` performs.

### Trace/template JIT lineage

The structure of this module follows the well-trodden path of
**method/template JIT compilers** (cf. Aycock, *"A Brief History of
Just-In-Time"*, ACM Computing Surveys, 2003). Rather than emitting code
for arbitrary control flow, we *canonicalize* the input tree to a
minimal closed form — a `(GenTag, seed)` automaton via `resolve_expr` —
and then bind a single specialized kernel. This mirrors the
"shape-then-specialize" strategy used by template-based JITs such as the
original HotSpot template interpreter and Lua's NaN-tagged dispatch
loops, where canonicalization collapses the dispatch space before code
is materialized.

### Stable ABI as the contract

The decision to keep the JIT output ABI-identical to the static path
reflects the classical compiler engineering principle of a **stable
calling convention as an isolation boundary** (the System V AMD64 ABI
and ARM AAPCS being the canonical references). By refusing to invent a
new in-memory representation of `AutomatonVM` / `NumVMStep` in IR, we
avoid re-deriving by-value vs. `sret` aggregate-passing rules per target
— a notoriously fragile undertaking. Instead the JIT specializes
*which* statically-linked kernel is invoked, never *how* it is invoked.

---

## Two Backends, One Entry Point

`compile()` selects its backend at compile time via `NAM_USE_LLVM_JIT`.

### 1. LLVM ORC backend (`NAM_USE_LLVM_JIT` defined)

When built against LLVM, the module pulls in ORC's `LLJIT`
(cf. Lattner & Adve, *"LLVM: A Compilation Framework for Lifelong
Program Analysis & Transformation"*, CGO 2004, and the ORC v2 design in
the LLVM ORC documentation). Native target initialization is performed
once via a function-local `static` guard (the Meyers singleton idiom),
ensuring thread-safe, idempotent setup of `InitializeNativeTarget` and
`InitializeNativeTargetAsmPrinter`.

Rather than re-declaring the `AutomatonVM` aggregate in IR, the native
path collapses runtime dispatch to one fixed, statically-linked kernel
pointer chosen from `rg.gen`. This is **ABI-correct by construction**:
the chosen kernel already *is* a `NumVMFn`, so there is no struct-passing
convention to re-guess. A `JitOwner` RAII type holds the `LLJIT` session
to keep emitted code resident for the lifetime of the `CompiledFn`.

### 2. Interpreter backend (default, no LLVM)

The default backend keeps the repository self-contained and CI-trivial
while preserving the same ABI contract. Its central engineering problem
is that a **raw C function pointer carries no captured state**: it cannot
close over a `ResolvedGen`. The module solves this with a classic
**trampoline + handle table** pattern:

- A fixed-size pool of distinct trampoline functions is generated at
  compile time via a template parameter pack:

  ```cpp
      template <size_t N> NumVMStep interp_trampoline(AutomatonVM s);
  ```

  Each instantiation `interp_trampoline<N>` is a *distinct* function
  with a *distinct* address, produced through
  `std::index_sequence` expansion in `make_pool`.

- Each trampoline `N` recovers its associated `ResolvedGen` from slot
  `N` of a thread-safe `interp_table()` and dispatches via `dispatch()`.

This is the well-known **trampoline technique** for synthesizing
"closures with C linkage" (see e.g. GCC's nested-function trampolines and
libffi's closure API). Unlike runtime code-patching trampolines, ours are
statically compiled; only the *binding* (slot → generator) is dynamic.

The pool size `kInterpPool = 64` bounds the number of simultaneously
live compiled interpreters. Slot lifetime is managed by an
`InterpOwner` held through a `std::shared_ptr` custom deleter, which
releases its table entry on destruction — a standard RAII resource-slot
pattern.

---

## Key Types

### `class CompiledFn`

A move-only handle (copy is deleted, following the *Rule of Five* for a
resource-owning type) that bundles:

| Member     | Purpose                                             |
|------------|-----------------------------------------------------|
| `fn_`      | The C-ABI `NumVMFn` step function.                  |
| `native_`  | `true` iff produced by the real LLVM JIT.           |
| `backend_` | Opaque `shared_ptr<void>` owning backend resources. |

Public surface:

- `NumVMFn fn() const` — the raw step function.
- `NumVMStep step(AutomatonVM) const` — convenience single-step call.
- `bool is_native() const` — backend introspection (JIT vs. interpreter).

The exposed `fn()` pointer is valid for exactly the lifetime of the
owning `CompiledFn`; this is the explicit lifetime contract that allows
the rest of the library to treat it as an ordinary function pointer.

### `detail::ResolvedGen` dispatch

`dispatch()` maps a resolved `GenTag` to the corresponding static step
function (`Rational::step`, `Sqrt::step`, `PAdic::step`), defaulting to
`Rational::step`. `gen_tag_name()` provides human-readable tags for
debug tracing.

---

## Debug Tracing

Defining `NAM_JIT_DEBUG=1` (the current default) emits a one-line
`stderr` diagnostic from `compile()` reporting the selected backend,
resolved generator tag, and — for the interpreter path — the bound slot:

```
    [nam::compile] backend=interp gen=Sqrt(1) slot=0
    [nam::compile] backend=native gen=Rational(0)
```

This supports the *observability* discipline appropriate for a
code-generating component, where the mapping from input tree to emitted
artifact is otherwise opaque.

---

## Thread Safety

- LLVM target initialization is guarded by a function-local `static`
  (thread-safe under the C++11 magic-statics rule, ISO/IEC 14882 §6.7).
- The interpreter table and slot allocation are protected by
  `interp_mutex()`. Trampolines take the lock only to *read* their slot
  pointer, then release it before dispatch, minimizing contention.

---

## Failure Modes

- **Interpreter pool exhaustion**: if more than `kInterpPool` compiled
  interpreters are simultaneously live, `compile()` throws
  `std::runtime_error("nam JIT(interp): trampoline pool exhausted")`.
  This is a deliberate bounded-resource policy rather than unbounded
  trampoline synthesis.

---

## References

- Y. Futamura, *"Partial Evaluation of Computation Process — An Approach
  to a Compiler-Compiler"*, Systems, Computers, Controls, 1971.
- J. Aycock, *"A Brief History of Just-In-Time"*, ACM Computing Surveys,
  35(2), 2003.
- C. Lattner, V. Adve, *"LLVM: A Compilation Framework for Lifelong
  Program Analysis & Transformation"*, CGO 2004.
- LLVM ORC v2 / `LLJIT` documentation (llvm.org).
- System V AMD64 ABI; ARM Architecture Procedure Call Standard (AAPCS).
- `THEORY.md`, *"LLVM as the Execution Substrate"* (this repository).