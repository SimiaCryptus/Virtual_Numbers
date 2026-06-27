// bindings/wasm/nam_wasm.cpp
//
// WebAssembly bindings for the ergonomic user layer (Phase 3 Number API),
// built with Emscripten + embind. Mirrors the pybind11 bindings exactly so
// the honesty commitments cross the WASM/JS boundary unchanged:
//
//   - Comparison maps to JS tri-state: -1 | 1 | null (pending), and
//     definitely_less_than to true | false | null.
//   - Fork cost is annotated by tier (fork_cost()).
//   - Memoization is an explicit mode (.streaming() / .cached(N)).
//   - Precision contexts are scoped: we expose enter/exit so a JS wrapper
//     can implement `with`-style nesting (see nam.js).
//
// We expose ONLY the combinator/user layer (Number), never the raw
// NumVMFn ABI, per THEORY.md "Most users never see the primitive layer".
#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "nam/number.hpp"
#include "nam/compare.hpp"

using namespace emscripten;
using nam::Number;
using nam::Trit;
using nam::PrecisionContext;

namespace {
    // A JS-friendly RAII context manager wrapping PrecisionContext. We hold
    // the guard in a unique_ptr so enter() starts the scope and exit()
    // restores the prior precision -- exactly the C++ semantics. The JS
    // wrapper (nam.js) calls these around a callback to emulate `with`.
    class WasmPrecisionContext {
    public:
        explicit WasmPrecisionContext(int digits) : digits_(digits) {
        }

        void enter() {
            guard_ = std::make_unique<PrecisionContext>(digits_);
        }

        void exit() {
            guard_.reset(); // restores prior precision on scope exit
        }

    private:
        int digits_;
        std::unique_ptr<PrecisionContext> guard_;
    };

    // Map a tri-state ordering (Trit) to a JS value:
    //   Less              -> -1
    //   Greater           ->  1
    //   Indistinguishable -> null  (honest pending)
    val trit_to_js(Trit t) {
        switch (t) {
            case Trit::Less: return val(-1);
            case Trit::Greater: return val(1);
            case Trit::Indistinguishable: return val::null();
        }
        return val::null();
    }

    // Map std::optional<bool> to JS true | false | null.
    val optbool_to_js(std::optional<bool> o) {
        if (!o.has_value()) return val::null();
        return val(*o);
    }

    // ----- Thin free-function wrappers (embind cannot bind through the
    //       std::optional / tuple return types directly) -----

    std::vector<uint32_t> number_digits_n(Number &n, int count) {
        return n.digits(count);
    }

    std::vector<uint32_t> number_digits_ctx(Number &n) {
        return n.digits();
    }

    val number_next_digit(Number &n) {
        auto d = n.next_digit();
        if (!d.has_value()) return val::null(); // honest pending
        return val(*d);
    }

    // Returns a JS array [a, b] of independent forks (value semantics).
    val number_fork(const Number &n) {
        auto pr = n.fork();
        val arr = val::array();
        arr.call<void>("push", pr.first);
        arr.call<void>("push", pr.second);
        return arr;
    }

    // Skip n digits (periodic automata only). Returns null when no
    // fast-forward path exists (e.g. the series tier).
    val number_skip(const Number &n, double k) {
        auto r = n.skip(static_cast<uint64_t>(k));
        if (!r.has_value()) return val::null();
        return val(*r);
    }

    val number_definitely_less_than(const Number &a, const Number &b,
                                    int max_digits) {
        return optbool_to_js(a.definitely_less_than(b, max_digits));
    }

    val number_compare(const Number &a, const Number &b, int max_digits) {
        return trit_to_js(a.compare(b, max_digits));
    }

    std::string number_repr(Number &n) {
        return std::string("<nam.Number base=") +
               std::to_string(n.base()) + " " + n.fork_cost() + ">";
    }

    // fork_cost() returns a const char*; embind cannot implicitly bind raw
    // pointers, so marshal it across the boundary as a std::string.
    std::string number_fork_cost(const Number &n) {
        return std::string(n.fork_cost());
    }


    int current_precision() { return PrecisionContext::digits(); }
} // namespace

EMSCRIPTEN_BINDINGS (nam_module) {
    // ---- digit vector <-> JS array ----
    register_vector<uint32_t>("DigitVector");

    // ---- Precision context (enter/exit; nesting handled in nam.js) ----
    class_<WasmPrecisionContext>("PrecisionContext")
            .constructor<int>()
            .function("enter", &WasmPrecisionContext::enter)
            .function("exit", &WasmPrecisionContext::exit);

    function("current_precision", &current_precision);

    // ---- The Number type ----
    class_<Number>("Number")
            // Introspection.
            .function("base", &Number::base)
            .function("fork_cost", &number_fork_cost)
            // Codec: base as projection.
            .function("in_base", &Number::in_base)
            // Memoization policy (explicit).
            .function("streaming", &Number::streaming)
            .function("cached", &Number::cached)
            // Fork (honest cost; value semantics).
            .function("fork", &number_fork)
            // Skip-ahead.
            .function("skip", &number_skip)
            // Digit emission.
            .function("next_digit", &number_next_digit)
            .function("digits", &number_digits_n)
            .function("digits_ctx", &number_digits_ctx)
            // Honest comparison predicates.
            .function("agrees_with", &Number::agrees_with)
            .function("definitely_less_than", &number_definitely_less_than)
            .function("compare", &number_compare)
            // Rendering.
            .function("to_string", &Number::to_string)
            .function("repr", &number_repr);

    // ---- Ergonomic constructors (mpmath-flavoured free functions) ----
    function("rational", &Number::rational);
    function("sqrt", select_overload < Number(uint64_t, uint32_t) > (&nam::sqrt));
    function("padic", &Number::padic);
    function("e", &Number::e);
    function("ln2", &Number::ln2);
    function("one_over_e", &Number::one_over_e);
    function("pi_quarter", &Number::pi_quarter);
    function("catalan", &Number::catalan);
}
