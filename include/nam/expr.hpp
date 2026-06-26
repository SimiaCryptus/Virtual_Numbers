// include/nam/expr.hpp
//
// Phase 4: a runtime-constructed expression tree over the automaton ABI.
//
// THEORY.md: "For runtime-constructed expression trees -- parsing
// arithmetic expressions, dynamic matrix construction, user-input
// formulas -- function-pointer dispatch will not be eliminated by static
// optimization alone. For these cases, the library provides a JIT
// compilation path: an expression tree is compiled to a single
// specialized LLVM function via compile(expr_tree) -> NumVMFn."
//
// The Expr tree is deliberately small and POD-friendly: a leaf names a
// built-in automaton generator (by tag) plus its seed AutomatonVM; an
// internal node is an affine codec wrapper (rebase) that reprojects the
// child into a new base. This keeps the JIT target a genuine NumVMFn:
// step(AutomatonVM) -> (digit, AutomatonVM), with all dispatch resolved
// at compile time.
#ifndef NAM_EXPR_HPP
#define NAM_EXPR_HPP

#include <cstdint>
#include <memory>
#include <vector>

#include "nam/abi.h"
#include "nam/rational.hpp"
#include "nam/algebraic.hpp"
#include "nam/padic.hpp"
#include "nam/codec.hpp"

namespace nam
{
    // The set of automaton generators the JIT/interpreter can specialize.
    // Matches Number::Gen but lives at the ABI-adjacent layer so the JIT
    // does not depend on the user tier.
    enum class GenTag : uint32_t
    {
        Rational = 0,
        Sqrt = 1,
        PAdic = 2,
    };

    // Node kinds for the runtime expression tree.
    enum class ExprKind : uint32_t
    {
        Leaf = 0, // a built-in generator with a seed AutomatonVM
        Rebase = 1, // reproject a Rational child into a new base (codec)
    };

    // A runtime expression tree node. Leaves carry the seed VM; Rebase
    // nodes carry exactly one child and a target base.
    struct Expr
    {
        ExprKind kind = ExprKind::Leaf;
        GenTag gen = GenTag::Rational; // valid when kind == Leaf
        AutomatonVM seed{}; // valid when kind == Leaf
        uint32_t rebase_to = 0; // valid when kind == Rebase
        std::shared_ptr<Expr> child; // valid when kind == Rebase

        // ----- Leaf builders -----
        static std::shared_ptr<Expr> leaf_rational(uint64_t p, uint64_t q,
                                                   uint32_t base)
        {
            auto e = std::make_shared<Expr>();
            e->kind = ExprKind::Leaf;
            e->gen = GenTag::Rational;
            e->seed = make_rational(p, q, base);
            return e;
        }

        static std::shared_ptr<Expr> leaf_sqrt(uint64_t D, uint32_t base)
        {
            auto e = std::make_shared<Expr>();
            e->kind = ExprKind::Leaf;
            e->gen = GenTag::Sqrt;
            e->seed = make_sqrt(D, base);
            return e;
        }

        static std::shared_ptr<Expr> leaf_padic(int64_t a, int64_t b,
                                                uint32_t p)
        {
            auto e = std::make_shared<Expr>();
            e->kind = ExprKind::Leaf;
            e->gen = GenTag::PAdic;
            e->seed = make_padic(a, b, p);
            return e;
        }

        // ----- Rebase (codec) node: only analytic for Rational leaves -----
        static std::shared_ptr<Expr> rebase(std::shared_ptr<Expr> child,
                                            uint32_t new_base)
        {
            auto e = std::make_shared<Expr>();
            e->kind = ExprKind::Rebase;
            e->rebase_to = new_base;
            e->child = std::move(child);
            return e;
        }
    };

    // Resolve an expression tree to its concrete (GenTag, seed) pair. For
    // the Phase 4 node set the tree always collapses to a single automaton
    // generator: Rebase on a Rational leaf is the analytic codec swap, and
    // a Leaf is itself. This is the canonicalization the JIT specializes.
    struct ResolvedGen
    {
        GenTag gen;
        AutomatonVM seed;
    };

    inline ResolvedGen resolve_expr(const Expr& e)
    {
        switch (e.kind)
        {
        case ExprKind::Leaf:
            return ResolvedGen{e.gen, e.seed};
        case ExprKind::Rebase:
        {
            ResolvedGen c = resolve_expr(*e.child);
            if (c.gen == GenTag::Rational)
            {
                // Analytic rational reprojection (codec.hpp).
                return ResolvedGen{GenTag::Rational,
                                   rational_in_base(c.seed, e.rebase_to)};
            }
            // Generic generators carry base in the VM directly.
            c.seed.base = e.rebase_to;
            return c;
        }
        }
        return ResolvedGen{e.gen, e.seed};
    }
} // namespace nam

#endif // NAM_EXPR_HPP