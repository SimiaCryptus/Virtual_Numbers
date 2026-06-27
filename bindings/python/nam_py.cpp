// bindings/nam_py.cpp
//
// Phase 3: pybind11 bindings for the ergonomic, mpmath/Decimal-flavoured
// user layer (THEORY.md "The User-Facing API").
//
// Design rules:
//   - Expose the combinator/user layer (Number), not the raw NumVMFn ABI.
//   - Comparison returns Python tri-state: True | False | None (pending),
//     never a false definite answer.
//   - Fork is honest about cost (fork_cost()) and is value-semantic.
//   - Memoization is an explicit mode (.streaming() / .cached(N)); there is
//     no hidden global cache.
//   - Precision contexts are scoped RAII, exposed as a Python context
//     manager so `with nam.precision_context(digits=50): ...` works.
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "nam/number.hpp"
#include "nam/compare.hpp"

namespace py = pybind11;
using nam::Number;
using nam::Trit;
using nam::PrecisionContext;

namespace
{
    // A Python-friendly RAII context manager wrapping PrecisionContext.
    // We hold the guard in a unique_ptr so __enter__ starts the scope and
    // __exit__ restores the prior precision -- exactly the C++ semantics.
    class PyPrecisionContext
    {
    public:
        explicit PyPrecisionContext(int digits) : digits_(digits)
        {
        }

        void enter()
        {
            guard_ = std::make_unique<PrecisionContext>(digits_);
        }

        void exit(const py::object&, const py::object&, const py::object&)
        {
            guard_.reset(); // restores prior precision on scope exit
        }

    private:
        int digits_;
        std::unique_ptr<PrecisionContext> guard_;
    };

    // Map a tri-state ordering (Trit) to a Python object:
    //   Less          -> -1
    //   Greater       ->  1
    //   Indistinguishable -> None  (honest pending)
    py::object trit_to_py(Trit t)
    {
        switch (t)
        {
        case Trit::Less: return py::int_(-1);
        case Trit::Greater: return py::int_(1);
        case Trit::Indistinguishable: return py::none();
        }
        return py::none();
    }

    // Map std::optional<bool> to Python True | False | None.
    py::object optbool_to_py(std::optional<bool> o)
    {
        if (!o.has_value()) return py::none();
        return py::bool_(*o);
    }
} // namespace

PYBIND11_MODULE(nam, m)
{
    m.doc() =
        "Numbers as Machines -- the ergonomic user layer.\n"
        "Every number is a forkable, deterministic digit-stream VM.\n"
        "Comparison is interval-honest (True | False | None); fork cost is\n"
        "annotated by tier; memoization is an explicit mode.";

    // ---- Trit enum (Less | Greater | Indistinguishable) ----
    py::enum_<Trit>(m, "Trit")
        .value("Less", Trit::Less)
        .value("Greater", Trit::Greater)
        .value("Indistinguishable", Trit::Indistinguishable);

    // ---- Precision context manager ----
    py::class_<PyPrecisionContext>(m, "PrecisionContext")
        .def(py::init<int>(), py::arg("digits"))
        .def("__enter__", [](PyPrecisionContext& self) -> PyPrecisionContext&
        {
            self.enter();
            return self;
        })
        .def("__exit__", &PyPrecisionContext::exit);

    m.def("precision_context",
          [](int digits) { return PyPrecisionContext(digits); },
          py::arg("digits"),
          "Scoped, thread-local precision context (RAII). Use as:\n"
          "    with precision_context(digits=50):\n"
          "        ...");

    m.def("current_precision", []() { return PrecisionContext::digits(); },
          "Return the current thread-local target digit count.");

    // ---- The Number type ----
    py::class_<Number> number(m, "Number");

    // Tier / Memo enums for introspection.
    py::enum_<Number::Tier>(number, "Tier")
        .value("Automaton", Number::Tier::Automaton)
        .value("Series", Number::Tier::Series);

    // ----- Constructors (static factories) -----
    number
        .def_static("rational", &Number::rational,
                    py::arg("p"), py::arg("q"), py::arg("base") = 10,
                    "Construct the fractional digits of (p mod q)/q.")
        .def_static("sqrt", &Number::sqrt,
                    py::arg("D"), py::arg("base") = 10,
                    "Construct the fractional digits of sqrt(D).")
        .def_static("padic", &Number::padic,
                    py::arg("a"), py::arg("b"), py::arg("p"),
                    "Construct a/b in Z_p (LSB-up digit stream).")
        .def_static("e", &Number::e, py::arg("base") = 10,
                    "Series-tier e (fractional value in [0,1)).")
        .def_static("ln2", &Number::ln2, py::arg("base") = 10,
                    "Series-tier ln 2.")
        .def_static("one_over_e", &Number::one_over_e, py::arg("base") = 10,
                    "Series-tier 1/e.")
        .def_static("pi_quarter", &Number::pi_quarter, py::arg("base") = 10,
                    "Series-tier pi/4 (fractional value 0.7853...).");
        number.def_static("catalan", &Number::catalan, py::arg("base") = 10,
                          "Series-tier Catalan's constant (0.9159...).");

    // ----- Introspection -----
    number
        .def("tier", &Number::tier)
        .def("base", &Number::base)
        .def("accumulator_bitwidth", &Number::accumulator_bitwidth,
             "Series-tier live accumulator bit-width (0 for automata).")
        .def("fork_cost", &Number::fork_cost,
             "Honest fork-cost annotation: 'O(1)' (automaton) or "
             "'O(log n)' (series).");

    // ----- Codec: base as projection -----
    number.def("in_base", &Number::in_base, py::arg("new_base"),
               "Reproject into a new base without mutating the original.");

    // ----- Memoization policy (explicit) -----
    number
        .def("streaming", &Number::streaming,
             "O(1)-space mode: no cache, sequential access only.")
        .def("cached", &Number::cached, py::arg("max_digits"),
             "Bounded LRU memoization with the given digit capacity.");

    // ----- Fork (honest cost; value semantics) -----
    number.def("fork", [](const Number& n)
               {
                   auto pr = n.fork();
                   return py::make_tuple(pr.first, pr.second);
               }, "Return (a, b) independent forks. O(1) for automaton, O(log n) "
               "for series; forks receive independent caches.");

    // ----- Skip-ahead -----
    number.def("skip", [](const Number& n, uint64_t k) -> py::object
               {
                   auto r = n.skip(k);
                   if (!r.has_value()) return py::none();
                   return py::cast(*r);
               }, py::arg("n"),
               "Skip n digits (periodic automata only). Returns None when no "
               "fast-forward path exists (e.g. the series tier).");

    // ----- Digit emission -----
    number
        .def("next_digit", [](Number& n) -> py::object
        {
            auto d = n.next_digit();
            if (!d.has_value()) return py::none(); // honest pending
            return py::int_(*d);
        }, "Emit the next digit, or None on an honest pending stall.")
        .def("digits", [](Number& n, int count) { return n.digits(count); },
             py::arg("n"),
             "Emit up to n digits (may be fewer at a boundary stall).")
        .def("digits", [](Number& n) { return n.digits(); },
             "Emit PrecisionContext::digits() digits.");

    // ----- Honest comparison predicates -----
    number
        .def("agrees_with", &Number::agrees_with,
             py::arg("other"), py::arg("digits"),
             "Exact agreement over a finite prefix of `digits` digits.")
        .def("definitely_less_than",
             [](const Number& a, const Number& b, int max_digits)
             {
                 return optbool_to_py(a.definitely_less_than(b, max_digits));
             }, py::arg("other"), py::arg("max_digits") = 30,
             "Returns True | False | None (pending). Never a false "
             "definite answer.")
        .def("compare",
             [](const Number& a, const Number& b, int max_digits)
             {
                 return trit_to_py(a.compare(b, max_digits));
             }, py::arg("other"), py::arg("max_digits") = 30,
             "Tri-state ordering: -1 (less), 1 (greater), None (pending).");

    // ----- Rendering -----
    number.def("to_string", &Number::to_string, py::arg("digits"),
               "Render '0.<digits>' in the current base; a trailing '?' "
               "marks an honest pending stall.");
    number.def("__repr__", [](Number& n)
    {
        return std::string("<nam.Number base=") +
            std::to_string(n.base()) + " " + n.fork_cost() + ">";
    });

    // ---- Free-function ergonomic constructors (mpmath-flavoured) ----
    m.def("rational", &Number::rational,
          py::arg("p"), py::arg("q"), py::arg("base") = 10);
    m.def("sqrt", [](uint64_t D, uint32_t base) { return nam::sqrt(D, base); },
          py::arg("D"), py::arg("base") = 10);
    m.def("e", &Number::e, py::arg("base") = 10);
    m.def("ln2", &Number::ln2, py::arg("base") = 10);
    m.def("one_over_e", &Number::one_over_e, py::arg("base") = 10);
    m.def("pi_quarter", &Number::pi_quarter, py::arg("base") = 10);
    m.def("catalan", &Number::catalan, py::arg("base") = 10);
}