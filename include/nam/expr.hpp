// include/nam/expr.hpp
//
// Phase 4: a runtime-constructed expression tree over automaton generators.
//
// With LLVM/JIT off the roadmap this is now a pure interpreter target: an
// expression resolves to a (GenTag, NumberSpace, State) triple. The old
// frozen AutomatonVM seed is replaced by a small variant over the concrete
// generator state types.
#ifndef NAM_EXPR_HPP
#define NAM_EXPR_HPP

#include <memory>
#include <variant>

#include "number_space.hpp"
#include "rational.hpp"
#include "algebraic.hpp"
#include "padic.hpp"

namespace nam {
    enum class GenTag : uint32_t {
        Rational = 0,
        Sqrt = 1,
        PAdic = 2,
    };

    using GenState = std::variant<Rational::State, Sqrt::State,
        PAdic::State>;

    enum class ExprKind : uint32_t {
        Leaf = 0,
        Rebase = 1,
    };

    struct Expr {
        ExprKind kind = ExprKind::Leaf;
        GenTag gen = GenTag::Rational;
        NumberSpace space{};
        GenState seed{Rational::State{}};
        uint32_t rebase_to = 0;
        std::shared_ptr<Expr> child;

        static std::shared_ptr<Expr> leaf_rational(uint64_t p, uint64_t q,
                                                   uint32_t base) {
            auto e = std::make_shared<Expr>();
            e->kind = ExprKind::Leaf;
            e->gen = GenTag::Rational;
            e->space = NumberSpace{base, Direction::LR, 0};
            e->seed = make_rational_state(p, q);
            return e;
        }

        static std::shared_ptr<Expr> leaf_sqrt(uint64_t D, uint32_t base) {
            auto e = std::make_shared<Expr>();
            e->kind = ExprKind::Leaf;
            e->gen = GenTag::Sqrt;
            e->space = NumberSpace{base, Direction::LR, 0};
            e->seed = make_sqrt_state(D);
            return e;
        }

        static std::shared_ptr<Expr> leaf_padic(int64_t a, int64_t b,
                                                uint32_t p) {
            auto e = std::make_shared<Expr>();
            e->kind = ExprKind::Leaf;
            e->gen = GenTag::PAdic;
            e->space = padic_space(p);
            e->seed = make_padic_state(a, b);
            return e;
        }

        static std::shared_ptr<Expr> rebase(std::shared_ptr<Expr> child,
                                            uint32_t new_base) {
            auto e = std::make_shared<Expr>();
            e->kind = ExprKind::Rebase;
            e->rebase_to = new_base;
            e->child = std::move(child);
            return e;
        }
    };

    struct ResolvedGen {
        GenTag gen;
        NumberSpace space;
        GenState seed;
    };

    inline ResolvedGen resolve_expr(const Expr &e) {
        switch (e.kind) {
            case ExprKind::Leaf:
                return ResolvedGen{e.gen, e.space, e.seed};
            case ExprKind::Rebase: {
                ResolvedGen c = resolve_expr(*e.child);
                // Rationals reproject analytically (state base-agnostic);
                // every generator simply rebinds the NumberSpace base.
                c.space = c.space.in_base(e.rebase_to);
                return c;
            }
        }
        return ResolvedGen{e.gen, e.space, e.seed};
    }
} // namespace nam

#endif // NAM_EXPR_HPP
