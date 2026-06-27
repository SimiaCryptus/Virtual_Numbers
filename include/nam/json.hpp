// include/nam/json.hpp
//
// A tiny zero-dependency JSON writer/reader sufficient for serializing
// number-machine seed configs and in-situ iterator state. This is NOT a
// general-purpose JSON library; it supports objects, arrays, strings,
// integers and the value shapes the nam state types actually emit.
//
// Honesty commitment: serialization is LOSSLESS for the automaton tier
// (the entire register file round-trips exactly). The series tier
// serializes its index + base + accumulator magnitudes so a deserialized
// VM resumes byte-identically.
#ifndef NAM_JSON_HPP
#define NAM_JSON_HPP

#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <sstream>

namespace nam::json {
    // ---- A minimal JSON value variant ----
    struct Value;
    using Array = std::vector<Value>;
    using Object = std::map<std::string, Value>;

    struct Value {
        enum class Type { Null, Bool, Int, Str, Arr, Obj };
        Type type = Type::Null;
        bool b = false;
        int64_t i = 0;
        std::string s;
        Array arr;
        Object obj;

        Value() = default;
        Value(const bool v) : type(Type::Bool), b(v) {}
        Value(const int64_t v) : type(Type::Int), i(v) {}
        Value(const int v) : type(Type::Int), i(v) {}
        Value(const uint32_t v) : type(Type::Int), i(static_cast<int64_t>(v)) {}
        Value(const uint64_t v) : type(Type::Int), i(static_cast<int64_t>(v)) {}
        Value(const char *v) : type(Type::Str), s(v) {}
        Value(std::string v) : type(Type::Str), s(std::move(v)) {}
        Value(Array v) : type(Type::Arr), arr(std::move(v)) {}
        Value(Object v) : type(Type::Obj), obj(std::move(v)) {}

        // Typed accessors (throw on type mismatch -- honest failure).
        [[nodiscard]] [[nodiscard]] [[nodiscard]] const Value &at(const std::string &k) const {
            if (type != Type::Obj) throw std::runtime_error("json: not an object");
            const auto it = obj.find(k);
            if (it == obj.end()) throw std::runtime_error("json: missing key " + k);
            return it->second;
      }

        [[nodiscard]] bool has(const std::string &k) const {
            return type == Type::Obj && obj.count(k) > 0;
        }

        [[nodiscard]] int64_t as_int() const {
            if (type != Type::Int) throw std::runtime_error("json: not an int");
            return i;
        }

        [[nodiscard]] const std::string &as_str() const {
            if (type != Type::Str) throw std::runtime_error("json: not a string");
            return s;
        }

        [[nodiscard]] const Array &as_arr() const {
            if (type != Type::Arr) throw std::runtime_error("json: not an array");
            return arr;
        }
    };

    // ---- Serialization ----
    inline void dump_string(const std::string &s, std::string &out) {
        out.push_back('"');
        for (const char c: s) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\t': out += "\\t"; break;
                case '\r': out += "\\r"; break;
                default: out.push_back(c);
            }
        }
        out.push_back('"');
    }

    inline void dump(const Value &v, std::string &out) {
        switch (v.type) {
            case Value::Type::Null: out += "null"; break;
            case Value::Type::Bool: out += v.b ? "true" : "false"; break;
            case Value::Type::Int: out += std::to_string(v.i); break;
            case Value::Type::Str: dump_string(v.s, out); break;
            case Value::Type::Arr: {
                out.push_back('[');
                for (size_t k = 0; k < v.arr.size(); ++k) {
                    if (k) out.push_back(',');
                    dump(v.arr[k], out);
                }
                out.push_back(']');
                break;
            }
            case Value::Type::Obj: {
                out.push_back('{');
                bool first = true;
                for (const auto &[key, val]: v.obj) {
                    if (!first) out.push_back(',');
                    first = false;
                    dump_string(key, out);
                    out.push_back(':');
                    dump(val, out);
                }
                out.push_back('}');
                break;
            }
        }
    }

    inline std::string dump(const Value &v) {
        std::string out;
        dump(v, out);
        return out;
    }

    // ---- Parsing ----
    class Parser {
    public:
        explicit Parser(const std::string &src) : s_(src) {}

        Value parse() {
            skip_ws();
            Value v = parse_value();
            skip_ws();
            return v;
        }

    private:
        const std::string &s_;
        size_t p_ = 0;

        void skip_ws() {
            while (p_ < s_.size() && (s_[p_] == ' ' || s_[p_] == '\n' ||
                    s_[p_] == '\t' || s_[p_] == '\r'))
                ++p_;
        }

        [[nodiscard]] char peek() const { return p_ < s_.size() ? s_[p_] : '\0'; }
        char get() { return p_ < s_.size() ? s_[p_++] : '\0'; }

        void expect(const char c) {
            if (get() != c)
                throw std::runtime_error(std::string("json: expected ") + c);
        }

        Value parse_value() {
            skip_ws();
            const char c = peek();
            if (c == '{') return parse_object();
            if (c == '[') return parse_array();
            if (c == '"') return Value(parse_string());
            if (c == 't' || c == 'f') return parse_bool();
            if (c == 'n') {
                p_ += 4; // null
                return Value();
            }
            return parse_int();
        }

        Value parse_object() {
            expect('{');
            Object o;
            skip_ws();
            if (peek() == '}') {
                get();
                return Value(std::move(o));
            }
            for (;;) {
                skip_ws();
                std::string key = parse_string();
                skip_ws();
                expect(':');
                Value val = parse_value();
                o.emplace(std::move(key), std::move(val));
                skip_ws();
                const char c = get();
                if (c == '}') break;
                if (c != ',') throw std::runtime_error("json: expected , or }");
            }
            return Value(std::move(o));
        }

        Value parse_array() {
            expect('[');
            Array a;
            skip_ws();
            if (peek() == ']') {
                get();
                return Value(std::move(a));
            }
            for (;;) {
                a.push_back(parse_value());
                skip_ws();
                const char c = get();
                if (c == ']') break;
                if (c != ',') throw std::runtime_error("json: expected , or ]");
            }
            return Value(std::move(a));
        }

        std::string parse_string() {
            expect('"');
            std::string out;
            for (;;) {
                const char c = get();
                if (c == '\0') throw std::runtime_error("json: unterminated string");
                if (c == '"') break;
                if (c == '\\') {
                    const char e = get();
                    switch (e) {
                        case '"': out.push_back('"'); break;
                        case '\\': out.push_back('\\'); break;
                        case 'n': out.push_back('\n'); break;
                        case 't': out.push_back('\t'); break;
                        case 'r': out.push_back('\r'); break;
                        default: out.push_back(e);
                    }
                } else {
                    out.push_back(c);
                }
            }
            return out;
        }

        Value parse_bool() {
            if (peek() == 't') {
                p_ += 4; // true
                return Value(true);
            }
            p_ += 5; // false
            return Value(false);
        }

        Value parse_int() {
            const size_t start = p_;
            if (peek() == '-') get();
            while (p_ < s_.size() &&
                   (s_[p_] >= '0' && s_[p_] <= '9'))
                ++p_;
            const std::string num = s_.substr(start, p_ - start);
            return Value(static_cast<int64_t>(std::strtoll(num.c_str(), nullptr, 10)));
        }
    };

    inline Value parse(const std::string &src) {
        Parser p(src);
        return p.parse();
    }
} // namespace nam::json

#endif // NAM_JSON_HPP