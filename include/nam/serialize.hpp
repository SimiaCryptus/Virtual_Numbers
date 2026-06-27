// include/nam/serialize.hpp
//
// JSON serialization for number machines + a stronger notion of
// seed-state configs and in-situ iterating machines.
//
// Three new capabilities sit here, above the generators and below (but
// adjacent to) the user-facing Number:
//
//   1. SeedConfig -- a fully-described, serializable specification of a
//      number machine: which generator family, the NumberSpace coordinate
//      frame, and the exact seed register file. A SeedConfig is the
//      canonical "recipe" for a machine; it round-trips losslessly through
//      JSON for the automaton tier and (index+accumulators) for series.
//
//   2. to_json / from_json -- lossless serialization of each concrete
//      generator State and the NumberSpace, plus SeedConfig and the
//      user-facing Number.
//
//   3. MachineIterator -- an in-situ iterating machine: a value that owns
//      its NumberSpace + live State and exposes operator*/operator++ so the
//      machine can be advanced in place (no external driver loop), forked
//      (value copy), serialized mid-stream, and resumed.
#ifndef NAM_SERIALIZE_HPP
#define NAM_SERIALIZE_HPP

#include <optional>
#include <stdexcept>
#include <variant>

#include "json.hpp"
#include "number_space.hpp"
#include "rational.hpp"
#include "algebraic.hpp"
#include "padic.hpp"
#include "series.hpp"

namespace nam {
    // ===================================================================
    // NumberSpace <-> JSON
    // ===================================================================
    inline json::Value to_json(const NumberSpace &ns) {
        json::Object o;
        o["base"] = json::Value(static_cast<int64_t>(ns.base));
        o["direction"] = json::Value(
            ns.direction == Direction::RL ? "RL" : "LR");
        o["scale"] = json::Value(static_cast<int64_t>(ns.scale));
        return json::Value(std::move(o));
    }

    inline NumberSpace number_space_from_json(const json::Value &v) {
        const auto base = static_cast<uint32_t>(v.at("base").as_int());
        const Direction dir = v.at("direction").as_str() == "RL"
                                  ? Direction::RL
                                  : Direction::LR;
        const auto scale = static_cast<int32_t>(v.at("scale").as_int());
        return NumberSpace{base, dir, scale};
    }

    // ===================================================================
    // BigInt <-> JSON (lossless via limb array + sign, plus a decimal
    // mirror for human inspection).
    // ===================================================================
    inline json::Value to_json(const BigInt &n) {
        json::Object o;
        json::Array limbs;
        for (const uint32_t l: n.limbs())
            limbs.push_back(json::Value(static_cast<int64_t>(l)));
        o["limbs"] = json::Value(std::move(limbs));
        o["neg"] = json::Value(n.sign());
        o["dec"] = json::Value(n.to_string()); // human-readable mirror
        return json::Value(std::move(o));
    }

    inline BigInt big_int_from_json(const json::Value &v) {
        // Prefer the lossless limb form; fall back to the decimal mirror.
        if (v.has("limbs")) {
            std::vector<uint32_t> limbs;
            for (const auto &l: v.at("limbs").as_arr())
                limbs.push_back(static_cast<uint32_t>(l.as_int()));
            const bool neg = v.has("neg") && v.at("neg").b;
            return BigInt::from_limbs(limbs, neg);
        }
        return BigInt::from_string(v.at("dec").as_str());
    }

    // ===================================================================
    // Concrete generator State <-> JSON
    // ===================================================================
    inline json::Value to_json(const Rational::State &s) {
        json::Object o;
        o["r"] = json::Value(static_cast<int64_t>(s.r));
        o["q"] = json::Value(static_cast<int64_t>(s.q));
        o["p"] = json::Value(static_cast<int64_t>(s.p));
        return json::Value(std::move(o));
    }

    inline Rational::State rational_state_from_json(const json::Value &v) {
        Rational::State s;
        s.r = static_cast<uint64_t>(v.at("r").as_int());
        s.q = static_cast<uint64_t>(v.at("q").as_int());
        s.p = static_cast<uint64_t>(v.at("p").as_int());
        return s;
    }

    inline json::Value to_json(const Sqrt::State &s) {
        json::Object o;
        // BoundedInt is two 64-bit limbs; serialize both halves.
        o["R_lo"] = json::Value(static_cast<int64_t>(s.R.lo));
        o["R_hi"] = json::Value(static_cast<int64_t>(s.R.hi));
        o["P"] = json::Value(static_cast<int64_t>(s.P));
        o["D"] = json::Value(static_cast<int64_t>(s.D));
        return json::Value(std::move(o));
    }

    inline Sqrt::State sqrt_state_from_json(const json::Value &v) {
        Sqrt::State s;
        s.R = BoundedInt(
            static_cast<uint64_t>(v.at("R_hi").as_int()),
            static_cast<uint64_t>(v.at("R_lo").as_int()));
        s.P = static_cast<uint64_t>(v.at("P").as_int());
        s.D = static_cast<uint64_t>(v.at("D").as_int());
        return s;
    }

    inline json::Value to_json(const PAdic::State &s) {
        json::Object o;
        o["a"] = json::Value(s.a);
        o["b"] = json::Value(s.b);
        return json::Value(std::move(o));
    }

    inline PAdic::State padic_state_from_json(const json::Value &v) {
        PAdic::State s;
        s.a = v.at("a").as_int();
        s.b = v.at("b").as_int();
        return s;
    }

    // ===================================================================
    // SeedConfig -- the canonical, serializable recipe for a machine.
    // ===================================================================
    enum class SeedGen : uint32_t { Rational = 0, Sqrt = 1, PAdic = 2 };

    inline const char *seed_gen_name(const SeedGen g) {
        switch (g) {
            case SeedGen::Rational: return "rational";
            case SeedGen::Sqrt: return "sqrt";
            case SeedGen::PAdic: return "padic";
        }
        return "rational";
    }

    inline SeedGen seed_gen_from_name(const std::string &n) {
        if (n == "sqrt") return SeedGen::Sqrt;
        if (n == "padic") return SeedGen::PAdic;
        return SeedGen::Rational;
    }

    // A SeedConfig pins everything needed to (re)materialize an automaton
    // machine: generator family + coordinate frame + seed register file.
    // The seed is the INITIAL state; advancing a machine never mutates its
    // SeedConfig (the config is the immutable recipe, the iterator carries
    // the live state).
    struct SeedConfig {
        SeedGen gen = SeedGen::Rational;
        NumberSpace space{};
        std::variant<Rational::State, Sqrt::State, PAdic::State> seed{
            Rational::State{}
        };

        // ---- Ergonomic builders mirroring Number's constructors. ----
        static SeedConfig rational(const uint64_t p, const uint64_t q,
                                   const uint32_t base = 10) {
            SeedConfig c;
            c.gen = SeedGen::Rational;
            c.space = NumberSpace{base, Direction::LR, 0};
            c.seed = make_rational_state(p, q);
            return c;
        }

        static SeedConfig sqrt(const uint64_t D, const uint32_t base = 10) {
            SeedConfig c;
            c.gen = SeedGen::Sqrt;
            c.space = NumberSpace{base, Direction::LR, 0};
            c.seed = make_sqrt_state(D);
            return c;
        }

        static SeedConfig padic(const int64_t a, const int64_t b,
                                const uint32_t p) {
            SeedConfig c;
            c.gen = SeedGen::PAdic;
            c.space = padic_space(p);
            c.seed = make_padic_state(a, b);
            return c;
        }

        // Reproject into a new base (base is a codec). The seed register
        // file is preserved; only the coordinate frame changes.
        [[nodiscard]] SeedConfig in_base(const uint32_t new_base) const {
            SeedConfig c = *this;
            c.space = space.in_base(new_base);
            return c;
        }
    };

    inline json::Value to_json(const SeedConfig &c) {
        json::Object o;
        o["kind"] = json::Value("seed_config");
        o["gen"] = json::Value(seed_gen_name(c.gen));
        o["space"] = to_json(c.space);
        switch (c.gen) {
            case SeedGen::Rational:
                o["seed"] = to_json(std::get<Rational::State>(c.seed));
                break;
            case SeedGen::Sqrt:
                o["seed"] = to_json(std::get<Sqrt::State>(c.seed));
                break;
            case SeedGen::PAdic:
                o["seed"] = to_json(std::get<PAdic::State>(c.seed));
                break;
        }
        return json::Value(std::move(o));
    }

    inline SeedConfig seed_config_from_json(const json::Value &v) {
        SeedConfig c;
        c.gen = seed_gen_from_name(v.at("gen").as_str());
        c.space = number_space_from_json(v.at("space"));
        switch (c.gen) {
            case SeedGen::Rational:
                c.seed = rational_state_from_json(v.at("seed"));
                break;
            case SeedGen::Sqrt:
                c.seed = sqrt_state_from_json(v.at("seed"));
                break;
            case SeedGen::PAdic:
                c.seed = padic_state_from_json(v.at("seed"));
                break;
        }
        return c;
    }

    inline std::string to_json_string(const SeedConfig &c) {
        return json::dump(to_json(c));
    }

    inline SeedConfig seed_config_from_json_string(const std::string &s) {
        return seed_config_from_json(json::parse(s));
    }

    // ===================================================================
    // MachineIterator -- an in-situ iterating automaton machine.
    //
    // Owns a NumberSpace + live State and advances IN PLACE. Unlike the
    // free `step` functions (which thread state functionally) this is a
    // stateful cursor: dereference reads the current digit, ++ advances.
    // It is a value type (O(1) fork = copy) and serializes mid-stream so a
    // long run can be checkpointed and resumed byte-identically.
    // ===================================================================
    class MachineIterator {
    public:
        MachineIterator() = default;

        explicit MachineIterator(const SeedConfig &cfg)
            : gen_(cfg.gen), space_(cfg.space), state_(cfg.seed) {
            prime();
        }

        MachineIterator(const SeedGen gen, const NumberSpace &space,
                        const std::variant<Rational::State, Sqrt::State,
                            PAdic::State> &state, const uint64_t pos = 0)
            : gen_(gen), space_(space), state_(state), pos_(pos) {
            prime();
        }

        // Current digit at the cursor.
        uint32_t operator*() const { return current_; }
        [[nodiscard]] uint32_t digit() const { return current_; }

        [[nodiscard]] // Position (number of digits consumed so far).
        [[nodiscard]] uint64_t position() const { return pos_; }

        // Advance the cursor in place. Returns *this for chaining.
        MachineIterator &operator++() {
            advance();
            return *this;
        }

        // Post-increment: snapshot, advance, return the snapshot digit.
        uint32_t next() {
            const uint32_t d = current_;
            advance();
            return d;
        }

        // Fork: value copy. O(1) for the automaton tier (trivially-copied
        // state). The two halves iterate independently.
        [[nodiscard]] MachineIterator fork() const { return *this; }

        // The generator family / coordinate frame (introspection).
        [[nodiscard]] SeedGen gen() const { return gen_; }
        [[nodiscard]] const NumberSpace &space() const { return space_; }

        // Re-derive a SeedConfig from the LIVE state (so a deserialized
        // iterator can spawn fresh machines from where it currently sits).
        [[nodiscard]] SeedConfig live_config() const {
            SeedConfig c;
            c.gen = gen_;
            c.space = space_;
            c.seed = state_;
            return c;
        }

        // ---- Serialization (lossless, includes live cursor) ----
        [[nodiscard]] json::Value to_json_value() const {
            json::Object o;
            o["kind"] = json::Value("machine_iterator");
            o["gen"] = json::Value(seed_gen_name(gen_));
            o["space"] = to_json(space_);
            o["pos"] = json::Value(static_cast<int64_t>(pos_));
            switch (gen_) {
                case SeedGen::Rational:
                    o["state"] = nam::to_json(std::get<Rational::State>(state_));
                    break;
                case SeedGen::Sqrt:
                    o["state"] = nam::to_json(std::get<Sqrt::State>(state_));
                    break;
                case SeedGen::PAdic:
                    o["state"] = nam::to_json(std::get<PAdic::State>(state_));
                    break;
            }
            return json::Value(std::move(o));
        }

        [[nodiscard]] std::string to_json_string() const {
            return json::dump(to_json_value());
        }

        static MachineIterator from_json(const json::Value &v) {
            MachineIterator it;
            it.gen_ = seed_gen_from_name(v.at("gen").as_str());
            it.space_ = number_space_from_json(v.at("space"));
            it.pos_ = static_cast<uint64_t>(v.at("pos").as_int());
            switch (it.gen_) {
                case SeedGen::Rational:
                    it.state_ = rational_state_from_json(v.at("state"));
                    break;
                case SeedGen::Sqrt:
                    it.state_ = sqrt_state_from_json(v.at("state"));
                    break;
                case SeedGen::PAdic:
                    it.state_ = padic_state_from_json(v.at("state"));
                    break;
            }
            it.prime();
            return it;
        }

        static MachineIterator from_json_string(const std::string &s) {
            return from_json(json::parse(s));
        }

    private:
        SeedGen gen_ = SeedGen::Rational;
        NumberSpace space_{};
        std::variant<Rational::State, Sqrt::State, PAdic::State> state_{
            Rational::State{}
        };
        uint64_t pos_ = 0;
        uint32_t current_ = 0;

        // Compute the digit at the current state WITHOUT advancing the
        // stored state (peek). Keeps the cursor semantics: operator* sees
        // the digit the live state would emit next.
        void prime() { current_ = peek_digit(); }

        [[nodiscard]] uint32_t peek_digit() const {
            switch (gen_) {
                case SeedGen::Rational:
                    return Rational::step(space_,
                                          std::get<Rational::State>(state_)).digit;
                case SeedGen::Sqrt:
                    return Sqrt::step(space_,
                                      std::get<Sqrt::State>(state_)).digit;
                case SeedGen::PAdic:
                    return PAdic::step(space_,
                                       std::get<PAdic::State>(state_)).digit;
            }
            return 0;
        }

        void advance() {
            switch (gen_) {
                case SeedGen::Rational: {
                    auto &s = std::get<Rational::State>(state_);
                    s = Rational::step(space_, s).next;
                    break;
                }
                case SeedGen::Sqrt: {
                    auto &s = std::get<Sqrt::State>(state_);
                    s = Sqrt::step(space_, s).next;
                    break;
                }
                case SeedGen::PAdic: {
                    auto &s = std::get<PAdic::State>(state_);
                    s = PAdic::step(space_, s).next;
                    break;
                }
            }
            ++pos_;
            current_ = peek_digit();
        }
    };

    // Materialize an in-situ iterating machine from a seed config.
    inline MachineIterator iterate(const SeedConfig &cfg) {
        return MachineIterator(cfg);
    }

    // ===================================================================
    // SeriesVM <-> JSON (series tier: index + base + accumulators).
    //
    // The immutable SeriesSpec (the compiled convergence proof) is NOT
    // serializable as data -- it is a code artifact. Callers must supply
    // the spec on deserialization (by name / factory). We serialize the
    // mutable accumulator state so a VM resumes byte-identically once the
    // spec is rebound.
    // ===================================================================
    inline json::Value to_json(const SeriesVM &vm) {
        json::Object o;
        o["kind"] = json::Value("series_vm");
        o["base"] = json::Value(static_cast<int64_t>(vm.base));
        o["index"] = json::Value(static_cast<int64_t>(vm.index));
        o["spec_name"] = json::Value(
            vm.spec ? vm.spec->name : "series");
        o["num"] = to_json(vm.num);
        o["den"] = to_json(vm.den);
        return json::Value(std::move(o));
    }

    // Rebind a deserialized SeriesVM onto a caller-supplied spec. The spec
    // name in the JSON is advisory; the caller owns spec selection.
    inline SeriesVM series_vm_from_json(
        const json::Value &v,
        std::shared_ptr<const SeriesSpec> spec) {
        SeriesVM vm;
        vm.base = static_cast<uint32_t>(v.at("base").as_int());
        vm.index = static_cast<uint64_t>(v.at("index").as_int());
        vm.spec = std::move(spec);
        vm.num = big_int_from_json(v.at("num"));
        vm.den = big_int_from_json(v.at("den"));
        return vm;
    }

    inline std::string spec_name_in_json(const json::Value &v) {
        return v.at("spec_name").as_str();
    }
} // namespace nam

#endif // NAM_SERIALIZE_HPP