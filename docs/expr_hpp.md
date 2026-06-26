# `nam/expr.hpp` — Runtime Expression Trees and the JIT Specialization Target

## Overview

This header defines the **Phase 4** runtime-constructed expression tree
that sits atop the *automaton ABI*. Where Phases 1–3 of the library rely on
statically-known number types and compile-time dispatch, Phase 4 addresses
the residual cases where the structure of a computation is only known at
runtime: parsing arithmetic expressions, building matrices from user input,
or evaluating formulas entered interactively.

For these dynamic cases, function-pointer dispatch cannot be eliminated by
static optimization alone. As `THEORY.md` states:

> "For runtime-constructed expression trees -- parsing arithmetic
> expressions, dynamic matrix construction, user-input formulas --
> function-pointer dispatch will not be eliminated by static optimization
> alone. For these cases, the library provides a JIT compilation path: an
> expression tree is compiled to a single specialized LLVM function via
> `compile(expr_tree) -> NumVMFn`."

The `Expr` tree is the input to that JIT path. It is deliberately small and
POD-friendly so that the compiled target is a genuine `NumVMFn`:

```
step(AutomatonVM) -> (digit, AutomatonVM)
```

with all dispatch resolved at compile time.

---

## Background and Theory

### The automaton model of numbers

The library follows a *coalgebraic* view of real and p-adic numbers: a
number is not a value but a **stream-producing automaton**. A number is
represented by a seed state (`AutomatonVM`) together with a transition
function `step` that emits one digit and yields a successor state. This is
the classic *final coalgebra* of streams over a digit alphabet (Rutten,
*Universal coalgebra: a theory of systems*, TCS 2000), and it underlies
exact real arithmetic frameworks such as Edalat & Potts' *redundant digit*
representations and the "infinite expansions" of Vuillemin (*Exact real
computer arithmetic with continued fractions*, IEEE TC 1990).

Concretely, the generators provided here correspond to well-studied
constructions:

- **Rational digit streams.** Long division in a fixed radix is an
  eventually-periodic automaton whose state is the running remainder. This
  is the elementary number-theoretic fact that `p/q` has an
  eventually-periodic base-`b` expansion with period dividing the
  multiplicative order of `b` modulo the part of `q` coprime to `b`
  (Hardy & Wright, *An Introduction to the Theory of Numbers*).

- **Quadratic irrationals (`sqrt`).** Square roots admit periodic
  continued-fraction expansions (Lagrange's theorem), and their radix
  expansions are produced by a digit-by-digit extraction automaton —
  essentially the schoolbook square-root algorithm recast as a state
  machine.

- **p-adic numbers.** The seed `(a, b, p)` denotes a p-adic rational whose
  Hensel expansion is again eventually periodic for rationals. The
  automaton emits p-adic digits least-significant-first, mirroring the
  classical Hensel lifting construction (Koblitz, *p-adic Numbers, p-adic
  Analysis, and Zeta-Functions*).

By unifying these under a single `step` ABI, the library treats archimedean
and non-archimedean number systems with the same dispatch machinery, in the
spirit of Ostrowski's theorem (the absolute values on `Q` are exactly the
real and the p-adic ones).

### Why a JIT is needed

Static dispatch elimination (monomorphization, devirtualization, inlining)
succeeds only when the call graph is known at compile time. A runtime
expression tree breaks this assumption: the shape of the tree — and hence
the sequence of `step` functions to invoke — is data, not code. The
standard response is **staged compilation** / *partial evaluation* (Futamura
projections; Jones, Gomard & Sestoft, *Partial Evaluation and Automatic
Program Generation*): specialize the generic interpreter with respect to a
specific tree to obtain a residual program with no interpretive overhead.

Here the residual program is exactly one `NumVMFn`. The expression tree is
first **canonicalized** to a single `(GenTag, AutomatonVM)` pair, then that
pair becomes the specialization data the LLVM backend bakes in.

### Rebase as an affine codec

The only internal node, `Rebase`, reprojects a child stream into a new
radix. For rational leaves this is an *analytic* base conversion: rather
than emitting digits in one base and re-encoding, the seed itself is
rewritten so that the same long-division automaton emits digits directly in
the target base. This is the role of `rational_in_base` in `codec.hpp`, and
it is precisely an affine reparametrization of the automaton state — a
*codec wrapper* in the terminology of stream transducers (Mealy machines
composed with the digit-producing coalgebra).

For non-rational generators, no closed-form rebase exists in the Phase 4
node set; the target base is simply recorded in the VM (`seed.base`) so the
generator's own `step` produces digits in that base.

---

## Design Notes

- **POD-friendliness.** `Expr` contains only trivially-copyable scalars plus
  a `shared_ptr` child. This keeps leaves cheap to construct and ensures the
  JIT can read the seed `AutomatonVM` as a flat blob of specialization
  constants.

- **ABI-adjacent enums.** `GenTag` deliberately mirrors `Number::Gen` but is
  defined at the ABI layer so the JIT does not depend on the user-facing
  tier. This decouples the compilation backend from the high-level API.

- **Single-generator collapse.** For the Phase 4 node set, every tree
  resolves to exactly one automaton generator. `Rebase` over a `Rational`
  leaf is the analytic codec swap; a bare `Leaf` is itself. This invariant
  is what makes the compile target a single `NumVMFn` rather than a tree
  walker.

---

## API Reference

### Enums

#### `enum class GenTag : uint32_t`
The set of automaton generators the JIT/interpreter can specialize.

| Value      | Meaning                              |
| ---------- | ------------------------------------ |
| `Rational` | Long-division digit automaton        |
| `Sqrt`     | Quadratic-irrational extraction      |
| `PAdic`    | Hensel-expansion p-adic automaton    |

#### `enum class ExprKind : uint32_t`
Node kinds for the runtime expression tree.

| Value    | Meaning                                                  |
| -------- | ------------------------------------------------------- |
| `Leaf`   | A built-in generator with a seed `AutomatonVM`          |
| `Rebase` | Reproject a `Rational` child into a new base (codec)    |

### `struct Expr`

A runtime expression tree node.

| Field        | Valid when            | Description                          |
| ------------ | --------------------- | ------------------------------------ |
| `kind`       | always                | Node discriminant                    |
| `gen`        | `kind == Leaf`        | Which generator the leaf names       |
| `seed`       | `kind == Leaf`        | Seed `AutomatonVM` for the generator |
| `rebase_to`  | `kind == Rebase`      | Target radix                         |
| `child`      | `kind == Rebase`      | The single rebased child             |

#### Leaf builders

```cpp
static std::shared_ptr<Expr> leaf_rational(uint64_t p, uint64_t q,
                                           uint32_t base);
static std::shared_ptr<Expr> leaf_sqrt(uint64_t D, uint32_t base);
static std::shared_ptr<Expr> leaf_padic(int64_t a, int64_t b, uint32_t p);
```

- `leaf_rational(p, q, base)` — the rational `p/q` as a base-`base` stream
  (via `make_rational`).
- `leaf_sqrt(D, base)` — `sqrt(D)` as a base-`base` stream (via
  `make_sqrt`).
- `leaf_padic(a, b, p)` — the p-adic rational `a/b` (via `make_padic`),
  with `p` the prime.

#### Rebase node

```cpp
static std::shared_ptr<Expr> rebase(std::shared_ptr<Expr> child,
                                    uint32_t new_base);
```

Wraps `child` in a base-reprojection node. Analytic only for `Rational`
leaves; for other generators the new base is recorded in the VM.

### Canonicalization

```cpp
struct ResolvedGen { GenTag gen; AutomatonVM seed; };
inline ResolvedGen resolve_expr(const Expr& e);
```

`resolve_expr` walks the tree and collapses it to a single
`(GenTag, AutomatonVM)` pair — the canonical form the JIT specializes:

- **Leaf** resolves to itself.
- **Rebase** resolves its child, then:
  - if the child is `Rational`, applies the analytic codec swap
    `rational_in_base(seed, rebase_to)`;
  - otherwise overwrites `seed.base = rebase_to`.

---

## Example

```cpp
using namespace nam;

// (7/22) re-expressed in base 16, then compiled to a NumVMFn.
auto e = Expr::rebase(Expr::leaf_rational(7, 22, 10), 16);

ResolvedGen rg = resolve_expr(*e);
// rg.gen == GenTag::Rational
// rg.seed is the long-division seed emitting base-16 digits directly.
```

---

## References

- J. Rutten, "Universal coalgebra: a theory of systems," *Theoretical
  Computer Science* 249(1), 2000.
- J. Vuillemin, "Exact real computer arithmetic with continued fractions,"
  *IEEE Transactions on Computers* 39(8), 1990.
- A. Edalat, P. Potts, "A new representation for exact real numbers,"
  *Electronic Notes in Theoretical Computer Science*, 1997.
- N. Jones, C. Gomard, P. Sestoft, *Partial Evaluation and Automatic Program
  Generation*, Prentice Hall, 1993.
- Y. Futamura, "Partial evaluation of computation process — an approach to a
  compiler-compiler," 1971 (reprinted, *Higher-Order and Symbolic
  Computation*, 1999).
- G. H. Hardy, E. M. Wright, *An Introduction to the Theory of Numbers*.
- N. Koblitz, *p-adic Numbers, p-adic Analysis, and Zeta-Functions*,
  Springer GTM 58.

## See also

- `nam/abi.h` — the `AutomatonVM` / `NumVMFn` ABI.
- `nam/codec.hpp` — `rational_in_base` analytic base conversion.
- `nam/rational.hpp`, `nam/algebraic.hpp`, `nam/padic.hpp` — the generators.