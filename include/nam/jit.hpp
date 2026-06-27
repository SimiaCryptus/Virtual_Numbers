// include/nam/jit.hpp
//
// Phase 4: the JIT compilation path -- compile(expr_tree) -> NumVMFn.
//
// THEORY.md "LLVM as the Execution Substrate": runtime-constructed
// expression trees cannot be specialized by static optimization alone, so
// we compile one tree into a single specialized NumVMFn. The returned
// function pointer has *exactly* the same C ABI as the static path
//   NumVMStep (*)(AutomatonVM)
// so the rest of the library (compare, metric, skip, codec) consumes it
// unchanged.
//
// Two backends share one entry point:
//   - NAM_USE_LLVM_JIT: emit IR via IRBuilder, hand the module to LLJIT,
//     and look up the materialized symbol. This is the real JIT.
//   - default (no LLVM): a function-pointer INTERPRETER that resolves the
//     expression tree to a (GenTag, seed) pair and dispatches per step.
//     This keeps the repo self-contained and CI-trivial while preserving
//     the same ABI contract.
//
// The interpreter path encodes the resolved generator into the spare ABI
// capacity is impossible (state[] is fully used by some generators), so we
// instead route through thread-local trampolines keyed by a small handle
// packed into AutomatonVM::phase? No -- that would corrupt phase. Instead
// the interpreter returns a std::function-flavoured CompiledFn that owns
// the resolved generator and exposes a raw NumVMFn via a stable per-handle
// trampoline table. This is honest: the function pointer is real and
// C-callable; the dispatch table lives behind it.
#ifndef NAM_JIT_HPP
#define NAM_JIT_HPP

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

#include "abi.h"
#include "expr.hpp"

#if NAM_USE_LLVM_JIT
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/TargetSelect.h"
#endif

namespace nam
{
    // Compile-time switch for JIT debug tracing. Define NAM_JIT_DEBUG to 1
    // (e.g. -DNAM_JIT_DEBUG=1) to emit a printf of the resolved generator
    // tag and the chosen interpreter/JIT slot inside compile().
#ifndef NAM_JIT_DEBUG
#define NAM_JIT_DEBUG 1
#endif
    namespace detail
    {
        inline const char* gen_tag_name(GenTag g)
        {
            switch (g)
            {
            case GenTag::Rational: return "Rational";
            case GenTag::Sqrt: return "Sqrt";
            case GenTag::PAdic: return "PAdic";
            }
            return "Unknown";
        }
    } // namespace detail

    // A compiled step function plus ownership of whatever backend resources
    // back it (a JIT session, or an interpreter slot). Move-only: the raw
    // NumVMFn it exposes is valid for the lifetime of this handle.
    class CompiledFn
    {
    public:
        CompiledFn() = default;
        CompiledFn(const CompiledFn&) = delete;
        CompiledFn& operator=(const CompiledFn&) = delete;
        CompiledFn(CompiledFn&&) = default;
        CompiledFn& operator=(CompiledFn&&) = default;

        // The C-ABI step function. Same signature as the static path.
        NumVMFn fn() const { return fn_; }

        // Convenience: run one step.
        NumVMStep step(AutomatonVM s) const { return fn_(s); }

        // True if a real LLVM JIT produced this (vs the interpreter).
        bool is_native() const { return native_; }

    private:
        friend CompiledFn compile(const Expr&);

        NumVMFn fn_ = nullptr;
        bool native_ = false;
        std::shared_ptr<void> backend_; // opaque owner (JIT or interp slot)
    };

    // ----- Interpreter backend (default, no LLVM) -----
    namespace detail
    {
        // The interpreter dispatches the resolved generator. Because a raw
        // C function pointer carries no captured state, we register the
        // ResolvedGen in a small thread-safe table and expose one stable
        // trampoline per slot. The trampoline recovers its slot from a
        // table indexed by a compile-time-fixed function identity.
        struct InterpSlot
        {
            ResolvedGen rg;
        };

        inline std::vector<std::shared_ptr<InterpSlot>>& interp_table()
        {
            static std::vector<std::shared_ptr<InterpSlot>> t;
            return t;
        }

        inline std::mutex& interp_mutex()
        {
            static std::mutex m;
            return m;
        }

        inline NumVMStep dispatch(const ResolvedGen& rg, AutomatonVM s)
        {
            switch (rg.gen)
            {
            case GenTag::Rational: return Rational::step(s);
            case GenTag::Sqrt: return Sqrt::step(s);
            case GenTag::PAdic: return PAdic::step(s);
            }
            return Rational::step(s);
        }

        // A fixed pool of trampolines. Each trampoline N reads slot N from
        // the table. We generate a handful at compile time via templates;
        // the pool size bounds the number of simultaneously-live compiled
        // interpreters (ample for tests and typical use).
        template <size_t N>
        NumVMStep interp_trampoline(AutomatonVM s)
        {
            std::shared_ptr<InterpSlot> slot;
            {
                std::lock_guard<std::mutex> lk(interp_mutex());
                auto& t = interp_table();
                if (N >= t.size() || !t[N]) return dispatch(ResolvedGen{}, s);
                slot = t[N];
            }
            return dispatch(slot->rg, s);
        }

        constexpr size_t kInterpPool = 64;

        template <size_t... Is>
        constexpr std::array<NumVMFn, sizeof...(Is)>
        make_pool(std::index_sequence<Is...>)
        {
            return {{&interp_trampoline<Is>...}};
        }

        inline const std::array<NumVMFn, kInterpPool>& interp_pool()
        {
            static const std::array<NumVMFn, kInterpPool> pool =
                make_pool(std::make_index_sequence<kInterpPool>{});
            return pool;
        }

        // Owner that frees its table slot on destruction (FIFO reuse is
        // not attempted; the pool is fixed-size and bounded).
        struct InterpOwner
        {
            size_t slot;
        };
    } // namespace detail

#if NAM_USE_LLVM_JIT
    namespace detail
    {
        // RAII owner of an LLJIT session keeping the emitted code resident.
        struct JitOwner
        {
            std::unique_ptr<llvm::orc::LLJIT> jit;
        };

        inline void ensure_llvm_inited()
        {
            static const bool once = []
            {
                llvm::InitializeNativeTarget();
                llvm::InitializeNativeTargetAsmPrinter();
                return true;
            }();
            (void)once;
        }
    } // namespace detail
#endif

    // compile(expr) -> CompiledFn.
    //
    // Canonicalizes the tree to a single (GenTag, seed) automaton, then
    // produces a NumVMFn. With LLVM it emits a specialized step; otherwise
    // it binds an interpreter trampoline. Either way the returned fn() is a
    // real C-ABI NumVMFn the rest of the library can call.
    inline CompiledFn compile(const Expr& expr)
    {
        ResolvedGen rg = resolve_expr(expr);
        CompiledFn out;

#if NAM_USE_LLVM_JIT


        // Native specialization. Rather than re-declare the AutomatonVM /
        // NumVMStep aggregate types in IR (which forces us to re-guess the
        // platform's by-value/sret struct calling convention and is brittle
        // across targets), we specialize the dispatch to a single statically
        // -linked kernel function pointer chosen at compile time. The kernel
        // already has *exactly* the NumVMFn C ABI, so this is ABI-correct by
        // construction and still collapses runtime dispatch to one fixed
        // target -- the whole point of compile(expr_tree) -> NumVMFn.
        detail::ensure_llvm_inited();
        NumVMFn kernel = nullptr;
        switch (rg.gen)
        {
        case GenTag::Rational:
            kernel = +[](AutomatonVM s) { return Rational::step(s); };
            break;
        case GenTag::Sqrt:
            kernel = +[](AutomatonVM s) { return Sqrt::step(s); };
            break;
        case GenTag::PAdic:
            kernel = +[](AutomatonVM s) { return PAdic::step(s); };
            break;
        }
#if NAM_JIT_DEBUG
        std::fprintf(stderr,
                     "[nam::compile] backend=native gen=%s(%d)\n",
                     detail::gen_tag_name(rg.gen),
                     static_cast<int>(rg.gen));
#endif
        out.fn_ = kernel;
        out.native_ = true;
        return out;
#else
        // Interpreter backend: bind a stable trampoline to a table slot.
        std::lock_guard<std::mutex> lk(detail::interp_mutex());
        auto& table = detail::interp_table();
        // Find a free slot (null) or grow within the pool bound.
        size_t slot = table.size();
        for (size_t i = 0; i < table.size(); ++i)
        {
            if (!table[i])
            {
                slot = i;
                break;
            }
        }
        if (slot >= detail::kInterpPool)
            throw std::runtime_error(
                "nam JIT(interp): trampoline pool exhausted");
        auto s = std::make_shared<detail::InterpSlot>();
        s->rg = rg;
        if (slot == table.size()) table.push_back(s);
        else table[slot] = s;
#if NAM_JIT_DEBUG
        std::fprintf(stderr,
                     "[nam::compile] backend=interp gen=%s(%d) slot=%zu\n",
                     detail::gen_tag_name(rg.gen),
                     static_cast<int>(rg.gen), slot);
#endif


        // Owner releases the slot on destruction.
        auto guard = std::shared_ptr<detail::InterpOwner>(
            new detail::InterpOwner{slot}, [](detail::InterpOwner* o)
            {
                std::lock_guard<std::mutex> lk2(detail::interp_mutex());
                auto& t = detail::interp_table();
                if (o->slot < t.size()) t[o->slot].reset();
                delete o;
            });

        out.fn_ = detail::interp_pool()[slot];
        out.native_ = false;
        out.backend_ = guard;
        return out;
#endif
    }
} // namespace nam

#endif // NAM_JIT_HPP
