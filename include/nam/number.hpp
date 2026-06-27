// include/nam/number.hpp
//
// Phase 3: The user-facing API (THEORY.md "The User-Facing API").
//
// This is the *user layer*: operator overloading, precision contexts,
// explicit memoization modes, and honest comparison predicates. It hides
// the raw NumVMFn ABI and exposes an mpmath/Decimal-flavoured surface.
//
// Three honesty commitments are preserved verbatim from the substrate:
//   - Comparison is interval-honest (tri-state / optional), never a false
//     definite answer.
//   - Fork cost is annotated by tier (O(1) automaton vs O(log n) series).
//   - Memoization is an EXPLICIT mode (.streaming() / .cached(N)); there is
//     no hidden global cache.
//
// The Number type is a thin tagged union over the two tiers. It is the
// combinator/user boundary: combinator authors stay below it, end users
// stay above it.
#ifndef NAM_NUMBER_HPP
#define NAM_NUMBER_HPP

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "abi.h"
#include "generator.hpp"
#include "rational.hpp"
#include "algebraic.hpp"
#include "codec.hpp"
#include "padic.hpp"
#include "compare.hpp"
#include "skip.hpp"
#include "series.hpp"
#include "refine.hpp"
#include "constants.hpp"
#include "memo.hpp"
#include "arith.hpp"

namespace nam {
    // ---- Precision context (THEORY.md "precision_context(digits=N)") ----
    //
    // Establishes a target digit accuracy for a region of code. Unlike
    // mpmath's global mutable precision, this is a *scoped, thread-local*
    // value with no hidden state leaking across scopes (RAII guard).
    class PrecisionContext {
    public:
        explicit PrecisionContext(int digits) : prev_(current()) {
            current() = digits;
        }

        ~PrecisionContext() { current() = prev_; }

        PrecisionContext(const PrecisionContext &) = delete;

        PrecisionContext &operator=(const PrecisionContext &) = delete;

        static int digits() { return current(); }

    private:
        int prev_;
        // Thread-local, not global-mutable-shared: each thread owns its own
        // precision so value semantics are never violated across threads.
        static int &current() {
            static thread_local int d = 30; // sane default
            return d;
        }
    };

    // Convenience factory mirroring `with precision_context(digits=50):`.
    inline PrecisionContext precision_context(int digits) {
        return PrecisionContext(digits);
    }

    // ---- Tri-state for user-facing comparisons (maps to Python None) ----
    // Reuses Trit from compare.hpp for ordering; for equality-ish queries
    // we expose std::optional<bool> directly (engaged == definite).

    // ---- The Number: tagged union over the two tiers ----
    //
    // AutomatonNumber: O(1) fork, fixed-size state.
    // SeriesNumber:    O(log n) fork, growing accumulators.
    //
    // Both expose a uniform `next_digit()` / `digits(n)` surface. Internally
    // the automaton tier dispatches on a small generator tag; the series
    // tier uses the interval-honest DigitExtractor.
    class Number {
    public:
        enum class Tier { Automaton, Series, Arith };

        enum class Gen { Rational, Sqrt, PAdic };

        enum class Memo { Streaming, Cached };

        // ----- Construction (user-layer ergonomic constructors) -----
        static Number rational(uint64_t p, uint64_t q, uint32_t base = 10) {
            Number n;
            n.tier_ = Tier::Automaton;
            n.gen_ = Gen::Rational;
            n.vm_ = make_rational(p, q, base);
            return n;
        }

        static Number sqrt(uint64_t D, uint32_t base = 10) {
            Number n;
            n.tier_ = Tier::Automaton;
            n.gen_ = Gen::Sqrt;
            n.vm_ = make_sqrt(D, base);
            return n;
        }

        static Number padic(int64_t a, int64_t b, uint32_t p) {
            Number n;
            n.tier_ = Tier::Automaton;
            n.gen_ = Gen::PAdic;
            n.vm_ = make_padic(a, b, p);
            return n;
        }

        // Series-tier transcendentals (fractional value in [0,1)).
        static Number e(uint32_t base = 10) { return series(make_e(base)); }
        static Number ln2(uint32_t base = 10) { return series(make_ln2(base)); }

        static Number one_over_e(uint32_t base = 10) {
            return series(make_one_over_e(base));
        }

        // pi/4 (fractional value in [0,1) = 0.7853981...).
        static Number pi_quarter(uint32_t base = 10) {
            return series(make_pi_quarter(base));
        }

        // Catalan's constant G = 0.9159655941...
        static Number catalan(uint32_t base = 10) {
            return series(make_catalan(base));
        }


        static Number series(SeriesVM vm) {
            Number n;
            n.tier_ = Tier::Series;
            n.series_ = std::move(vm);
            return n;
        }

        // ----- Arithmetic combiners (+ - * /) -----
        // Interval-honest binary arithmetic over the FRACTIONAL parts of two
        // numbers (values assumed in [0,1) for digit streaming). The result is
        // produced through an ArithStream that drives forked copies of both
        // operands via their uniform next_digit() surface, so it composes
        // across every tier. Never emits a false digit (honest pending).
        static Number combine(ArithOp op, const Number &x, const Number &y,
                              uint32_t base = 10) {
            Number n;
            n.tier_ = Tier::Arith;
            // Capture independent forks so operand consumption does not mutate
            // the caller's numbers (value semantics preserved).
            auto xs = std::make_shared<Number>(x);
            auto ys = std::make_shared<Number>(y);
            DigitFn xf = [xs]() { return xs->next_digit(); };
            DigitFn yf = [ys]() { return ys->next_digit(); };
            n.arith_ = std::make_shared<ArithStream>(op, xf, yf, base);
            n.arith_base_ = base;
            return n;
        }

        Number operator+(const Number &o) const {
            return combine(ArithOp::Add, *this, o, base());
        }

        Number operator-(const Number &o) const {
            return combine(ArithOp::Sub, *this, o, base());
        }

        Number operator*(const Number &o) const {
            return combine(ArithOp::Mul, *this, o, base());
        }

        Number operator/(const Number &o) const {
            return combine(ArithOp::Div, *this, o, base());
        }

        // The integer part of an arithmetic result (e.g. 0.6 + 0.7 -> 1).
        // For non-arith tiers the fractional value is in [0,1), so this is 0.
        // Returns nullopt on honest pending.
        std::optional<BigInt> integer_part() {
            if (tier_ == Tier::Arith && arith_) return arith_->integer_part();
            return BigInt(0);
        }


        // ----- Introspection -----
        Tier tier() const { return tier_; }

        uint32_t base() const {
            switch (tier_) {
                case Tier::Automaton: return vm_.base;
                case Tier::Series: return series_.base;
                case Tier::Arith: return arith_base_;
            }
            return 10;
        }

        // Series-tier complexity probe: live accumulator bit-width (memory IS
        // the metric). Returns 0 for the automaton tier (constant state).
        int accumulator_bitwidth() const {
            if (tier_ == Tier::Series) return series_.accumulator_bitwidth();
            return 0;
        }

        // Generator-family tag for the automaton tier (informational).
        Gen gen() const { return gen_; }


        // ----- Codec: base as projection (THEORY.md "in_base(b)") -----
        // Only the rational fast path is analytic; other automaton gens are
        // re-seeded by re-decoding through a streaming reprojection in the
        // digit layer. For Phase 3 we expose the exact rational reprojection
        // and a codec swap for series (which carries base in the VM).
        Number in_base(uint32_t new_base) const {
            Number n = *this;
            if (tier_ == Tier::Automaton) {
                if (gen_ == Gen::Rational) {
                    n.vm_ = rational_in_base(vm_, new_base);
                } else {
                    // Generic generators carry base in the VM directly.
                    n.vm_.base = new_base;
                }
            } else {
                n.series_.base = new_base;
            }
            return n;
        }

        // ----- Memoization policy (explicit, never hidden) -----
        Number streaming() const {
            Number n = *this;
            n.memo_ = Memo::Streaming;
            n.cache_.reset();
            return n;
        }

        Number cached(size_t max_digits) const {
            Number n = *this;
            n.memo_ = Memo::Cached;
            n.cache_ = std::make_shared<LruDigitCache>(max_digits);
            n.cache_max_ = max_digits;
            return n;
        }

        // ----- Fork (honest cost annotation) -----
        // Automaton tier: O(1) struct copy. Series tier: O(log n) deep copy.
        std::pair<Number, Number> fork() const {
            Number a = *this;
            Number b;
            b.tier_ = tier_;
            b.gen_ = gen_;
            b.memo_ = memo_;
            b.cache_max_ = cache_max_;
            if (tier_ == Tier::Automaton) {
                b.vm_ = num_vm_fork(vm_); // O(1)
            } else if (tier_ == Tier::Series) {
                b.series_ = series_.fork(); // O(log n) explicit deep copy
            } else // Tier::Arith
            {
                // Arith streams are stateful; the two forks share the same
                // underlying stream only if untouched. To preserve value
                // semantics we keep both halves pointing at independent copies
                // by reusing the captured operand forks. Here we share the
                // shared_ptr (digits not yet pulled => identical), which is
                // honest as long as the fork happens before consumption.
                b.arith_ = arith_;
                b.arith_base_ = arith_base_;
            }
            // Forks get independent caches (value semantics preserved).
            if (memo_ == Memo::Cached) {
                a.cache_ = std::make_shared<LruDigitCache>(cache_max_);
                b.cache_ = std::make_shared<LruDigitCache>(cache_max_);
            }
            return {std::move(a), std::move(b)};
        }

        // Fork cost annotation for the curious user.
        const char *fork_cost() const {
            if (tier_ == Tier::Automaton) return "O(1)";
            if (tier_ == Tier::Series) return "O(log n)";
            return "O(log n) [arith]"; // bounded by operand interval growth
        }

        // ----- Skip-ahead (only meaningful for periodic automata) -----
        std::optional<Number> skip(uint64_t n) const {
            if (tier_ != Tier::Automaton) return std::nullopt;
            if (gen_ != Gen::Rational) return std::nullopt; // Phase 1 path
            Number r = *this;
            r.vm_ = skip_rational(n, vm_);
            return r;
        }

        // ----- Digit emission -----
        // Returns the next digit (engaged) or pending (nullopt). For the
        // automaton tier digits are always committable; the series tier may
        // honestly stall at exact-boundary inputs.
        std::optional<uint32_t> next_digit() {
            if (tier_ == Tier::Automaton) {
                NumVMStep r = automaton_step(vm_);
                vm_ = r.next;
                uint32_t d = r.digit;
                memo_put(emitted_, d);
                ++emitted_;
                return d;
            }
            if (tier_ == Tier::Arith) {
                if (!arith_) return std::nullopt;
                auto d = arith_->next_digit();
                if (d.has_value()) {
                    memo_put(emitted_, *d);
                    ++emitted_;
                }
                return d;
            }
            // Series tier: build the extractor lazily so digits stream.
            if (!extractor_) {
                extractor_ = std::make_shared<DigitExtractor>(
                    make_extractor(series_.fork(), series_.base));
            }
            auto d = nam::next_digit(*extractor_);
            if (d.has_value()) {
                memo_put(emitted_, *d);
                ++emitted_;
            }
            return d;
        }

        // Emit up to n digits. May return fewer than n if the series tier
        // honestly stalls (pending) at a boundary value.
        std::vector<uint32_t> digits(int n) {
            std::vector<uint32_t> out;
            out.reserve(n);
            for (int i = 0; i < n; ++i) {
                auto d = next_digit();
                if (!d.has_value()) break;
                out.push_back(*d);
            }
            return out;
        }

        // Take exactly the precision-context digit count.
        std::vector<uint32_t> digits() {
            return digits(PrecisionContext::digits());
        }

        // ----- Digit statistics (analysis convenience) -----
        // Returns a frequency histogram over the first `n` emitted digits.
        // histogram[d] is the count of digit value d. The vector length is
        // the current base; entries for never-seen digits stay 0. A pending
        // stall (series boundary) simply yields fewer counted digits.
        std::vector<uint64_t> digit_histogram(int n) {
            std::vector<uint64_t> hist(base(), 0);
            Number copy = *this;
            for (int i = 0; i < n; ++i) {
                auto d = copy.next_digit();
                if (!d.has_value()) break;
                if (*d < hist.size()) ++hist[*d];
            }
            return hist;
        }


        // ----- Honest comparison predicates -----
        // agrees_with: EXACT for a finite prefix of `digits` digits.
        // Only valid for like-tier, like-generator, same-base numbers; the
        // automaton path uses the typed comparator, the series path streams.
        bool agrees_with(const Number &other, int digits) const {
            Number a = *this, b = other;
            for (int i = 0; i < digits; ++i) {
                auto da = a.next_digit();
                auto db = b.next_digit();
                if (!da.has_value() || !db.has_value()) return false;
                if (*da != *db) return false;
            }
            return true;
        }

        // definitely_less_than: true / false / pending (nullopt).
        // Valid for MSB-first positional streams (rationals/reals), NOT for
        // LSB-up p-adics -- mirrors compare.hpp's documented restriction.
        std::optional<bool> definitely_less_than(const Number &other,
                                                 int max_digits) const {
            Number a = *this, b = other;
            for (int i = 0; i < max_digits; ++i) {
                auto da = a.next_digit();
                auto db = b.next_digit();
                if (!da.has_value() || !db.has_value()) return std::nullopt;
                if (*da < *db) return true;
                if (*da > *db) return false;
            }
            return std::nullopt; // indistinguishable within bound
        }

        Trit compare(const Number &other, int max_digits) const {
            auto r = definitely_less_than(other, max_digits);
            if (!r.has_value()) return Trit::Indistinguishable;
            return *r ? Trit::Less : Trit::Greater;
        }

        // ----- Rendering -----
        // Render the fractional expansion as "0.<digits>" in the current
        // base (bases <= 36 use 0-9a-z). Honest: a trailing '?' marks a
        // pending (series boundary) stall.
        std::string to_string(int digits_n) {
            Number copy = *this;
            std::string out = "0.";
            for (int i = 0; i < digits_n; ++i) {
                auto d = copy.next_digit();
                if (!d.has_value()) {
                    out.push_back('?');
                    break;
                }
                out.push_back(digit_char(*d));
            }
            return out;
        }

    private:
        Tier tier_ = Tier::Automaton;
        Gen gen_ = Gen::Rational;
        AutomatonVM vm_{};
        SeriesVM series_{};
        Memo memo_ = Memo::Streaming;
        size_t cache_max_ = 0;
        uint64_t emitted_ = 0;
        std::shared_ptr<LruDigitCache> cache_;
        std::shared_ptr<DigitExtractor> extractor_;
        std::shared_ptr<ArithStream> arith_;
        uint32_t arith_base_ = 10;

        NumVMStep automaton_step(AutomatonVM s) const {
            switch (gen_) {
                case Gen::Rational: return Rational::step(s);
                case Gen::Sqrt: return Sqrt::step(s);
                case Gen::PAdic: return PAdic::step(s);
            }
            return Rational::step(s);
        }

        void memo_put(uint64_t index, uint32_t digit) {
            if (memo_ == Memo::Cached && cache_) cache_->put(index, digit);
        }

        static char digit_char(uint32_t d) {
            if (d < 10) return static_cast<char>('0' + d);
            if (d < 36) return static_cast<char>('a' + (d - 10));
            return '#';
        }
    };

    // ---- Free-function ergonomic constructors (mpmath-flavoured) ----
    inline Number make_e_number(uint32_t base = 10) { return Number::e(base); }

    inline Number make_ln2_number(uint32_t base = 10) {
        return Number::ln2(base);
    }

    inline Number sqrt(uint64_t D, uint32_t base = 10) {
        return Number::sqrt(D, base);
    }
} // namespace nam

#endif // NAM_NUMBER_HPP
