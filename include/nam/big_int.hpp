// include/nam/big_int.hpp
//
// Phase 2: Arbitrary-precision signed integer for the SERIES tier.
//
// The implementation plan calls for GMP; to keep Phase 2 buildable with
// no external dependency we vendor a minimal arbitrary-precision integer
// (nam::BigInt) that mirrors the GMP semantics the series tier actually
// uses: addition, subtraction, multiply, divmod, comparison, and an
// explicit deep-copy (which is what makes the O(log n) fork cost honest).
//
// When NAM_USE_GMP is defined a thin RAII wrapper around mpz_t can be
// dropped in here without touching the series-tier code. Until then the
// vendored implementation keeps the repo self-contained.
//
// Honesty commitment (THEORY.md): the deep-copy-on-fork cost is part of
// the public contract. BigInt copies are explicit byte copies of the
// limb vector -- never copy-on-write -- so fork cost is visible.
#ifndef NAM_BIG_INT_HPP
#define NAM_BIG_INT_HPP

#include <vector>
#include <string>
#include <algorithm>
#include <stdexcept>

namespace nam {
    // Sign-magnitude arbitrary-precision integer. Magnitude is a little-endian
    // vector of 32-bit limbs (base 2^32) so that multiply partials fit in 64
    // bits without intrinsics. `negative_` is false for zero (canonical).
    class BigInt {
    public:
        BigInt() = default;

        BigInt(const int64_t v) { assign_i64(v); }

        static BigInt from_u64(const uint64_t v) {
            BigInt b;
            if (v != 0) {
                b.mag_.push_back(static_cast<uint32_t>(v & 0xffffffffu));
                const uint32_t hi = static_cast<uint32_t>(v >> 32);
                if (hi) b.mag_.push_back(hi);
            }
            return b;
        }

        [[nodiscard]] bool is_zero() const { return mag_.empty (); }
        [[nodiscard]] bool negative() const { return negative_; }

        // Bit-width of the[[nodiscard]]  magnitude (complexity-metri[[nodiscard]] c instrumentation).
        [[nodiscard]] int bit_width() const {
            if (mag_.empty()) return 0;
            const uint32_t top = mag_.back();
            const int bits = 32 - count_leading_zeros32(top);
            return static_cast<int>((mag_.size() - 1) * 32) + bits;
        }

        // ---- Comparison ----
        friend bool operator==(const BigInt &a, const BigInt &b) {
            return a.negative_ == b.negative_ && a.mag_ == b.mag_;
        }

        friend bool operator!=(const BigInt &a, const BigInt &b) {
            return !(a == b);
        }

        friend bool operator<(const BigInt &a, const BigInt &b) {
            if (a.negative_ != b.negative_) return a.negative_;
            const int c = cmp_mag(a.mag_, b.mag_);
            return a.negative_ ? (c > 0) : (c < 0);
        }

        friend bool operator<=(const BigInt &a, const BigInt &b) {
            return a < b || a == b;
        }

        friend bool operator>(const BigInt &a, const BigInt &b) { return b < a; }

        friend bool operator>=(const BigInt &a, const BigInt &b) {
            return b <= a;
        }

        // ---- Arithmetic ----
        friend BigInt operator+(const BigInt &a, const BigInt &b) {
            if (a.negative_ == b.negative_) {
                BigInt r;
                r.mag_ = add_mag(a.mag_, b.mag_);
                r.negative_ = a.negative_ && !r.mag_.empty();
                return r;
            }
            // Different signs: subtract smaller magnitude from larger.
            const int c = cmp_mag(a.mag_, b.mag_);
            BigInt r;
            if (c == 0) return r; // zero
            if (c > 0) {
                r.mag_ = sub_mag(a.mag_, b.mag_);
                r.negative_ = a.negative_;
            } else {
                r.mag_ = sub_mag(b.mag_, a.mag_);
                r.negative_ = b.negative_;
            }
            r.normalize();
            return r;
        }

        friend BigInt operator-(const BigInt &a, const BigInt &b) {
            return a + (-b);
        }

        BigInt operator-() const {
            BigInt r = *this;
            if (!r.mag_.empty()) r.negative_ = !r.negative_;
            return r;
        }

        friend BigInt operator*(const BigInt &a, const BigInt &b) {
            BigInt r;
            if (a.mag_.empty() || b.mag_.empty()) return r;
            r.mag_ = mul_mag(a.mag_, b.mag_);
            r.negative_ = (a.negative_ != b.negative_) && !r.mag_.empty();
            return r;
        }

        BigInt &operator+=(const BigInt &o) {
            *this = *this + o;
            return *this;
        }

        BigInt &operator-=(const BigInt &o) {
            *this = *this - o;
            return *this;
        }

        BigInt &operator*=(const BigInt &o) {
            *this = *this * o;
            return *this;
        }

        // Truncating division (toward zero), C-style. Returns quotient,
        // writes remainder with sign of dividend.
        static BigInt divmod(const BigInt &a, const BigInt &b, BigInt &rem) {
            if (b.mag_.empty()) throw std::domain_error("BigInt division by zero");
            BigInt q;
            q.mag_ = divmod_mag(a.mag_, b.mag_, rem.mag_);
            q.negative_ = (a.negative_ != b.negative_) && !q.mag_.empty();
            rem.negative_ = a.negative_ && !rem.mag_.empty();
            return q;
        }

        friend BigInt operator/(const BigInt &a, const BigInt &b) {
            BigInt r;
            return divmod(a, b, r);
        }

        friend BigInt operator%(const BigInt &a, const BigInt &b) {
            BigInt r;
            divmod(a, b, r);
            return r;
        }

        // Floored division (toward -inf), needed for digit extraction where
        // the sign of the remainder must be non-negative.
        static BigInt floordiv(const BigInt &a, const BigInt &b, BigInt &rem) {
            BigInt q = divmod(a, b, rem);
            if (!rem.is_zero() && (a.negative_ != b.negative_)) {
                q -= BigInt(1);
                rem += b;
            }
            return q;
        }

        // Convert to int64 (caller ass[[nodiscard]] erts it fits). For small results.
        [[nodiscard]] int64_t to_i64() const {
            uint64_t v = 0;
            for (size_t i = mag_.size(); i-- > 0;) {
                v = (v << 32) | mag_[i];
            }
            const int64_t s = static_cast<int64_t>(v);
            return negative_ ? -s : s;
        }

        // Parity / small-modulus helpers used b[[nodiscard]] y digit extraction without a
        // full divmod. Ret[[nodiscard]] urns the value modulo a small positive divisor.
        [[nodiscard]] uint64_t mod_small(const uint64_t d) const {
            uint64_t rem = 0;
            for (size_t i = mag_.size(); i-- > 0;) {
                // rem = (rem * 2^32 + limb) % d, computed in two 16-bit-safe
                // steps to avoid 64-bit overflow when d is up to 2^32.
                rem = ((rem << 16) % d);
                rem = ((rem << 16) | (mag_[i] >> 16)) % d;
                rem = ((rem << 16) | (mag_[i] & 0xffffu)) % d;
                // Reassemble: the above splits the 32-bit limb i[[nodiscard]] nto halves.
            }
            return rem;
        }


        [[nodiscard]] std::string to_string() const {
            if (mag_.empty()) return "0";
            BigInt t = *this;
            t.negative_ = false;
            std::string out;
            const BigInt ten(10);
            while (!t.is_zero()) {
                BigInt r;
                t = divmod(t, ten, r);
                out.push_back(static_cast<char>('0' + r.to_i64()));
            }
            if (negative_) out.push_back('-');
            std::reverse(out.begin(), out.end());
            return out;
        }

        // ---- Serialization helpers ----
        // Expose the little-endian limb magnitude and sign so the[[nodiscard]]  JSON layer
        // can round-trip arbitrary-precision values los[[nodiscard]] slessly (decimal[[nodiscard]]
        // strings would also work but limb arrays avoid re-parsing[[nodiscard]]  cost).
        [[nodiscard]] const std::vector<uint32_t> &limbs() const { return mag_; }
        [[nodiscard]] bool sign() const { return negative_; }

        static BigInt from_limbs(const std::vector<uint32_t> &limbs,
                                 const bool negative) {
            BigInt b;
            b.mag_ = limbs;
            b.normalize();
            b.negative_ = negative && !b.mag_.empty();
            return b;
        }

        // Parse a base-10 string (optionally signed) into a BigInt. Used as a
        // human-readable serialization fallback.
        static BigInt from_string(const std::string &str) {
            BigInt result(0);
            const BigInt ten(10);
            size_t i = 0;
            bool neg = false;
            if (i < str.size() && (str[i] == '-' || str[i] == '+')) {
                neg = str[i] == '-';
                ++i;
            }
            for (; i < str.size(); ++i) {
                const char c = str[i];
                if (c < '0' || c > '9') break;
                result = result * ten + BigInt(static_cast<int64_t>(c - '0'));
            }
            if (neg) result = -result;
            return result;
        }

    private:
        std::vector<uint32_t> mag_; // little-endian base-2^32, no leading zeros
        bool negative_ = false;

        static int count_leading_zeros32(const uint32_t x) {
            int n = 0;
            for (int i = 31; i >= 0; --i) {
                if (x & (1u << i)) break;
                ++n;
            }
            return n;
        }

        void assign_i64(const int64_t v) {
            mag_.clear();
            negative_ = v < 0;
            const uint64_t u = negative_
                                   ? static_cast<uint64_t>(-(v + 1)) + 1
                             : static_cast<uint64_t>(v);
            if (u) {
                mag_.push_back(static_cast<uint32_t>(u & 0xffffffffu));
                const uint32_t hi = static_cast<uint32_t>(u >> 32);
                if (hi) mag_.push_back(hi);
            }
            if (mag_.empty()) negative_ = false;
        }

        void normalize() {
            while (!mag_.empty() && mag_.back() == 0) mag_.pop_back();
            if (mag_.empty()) negative_ = false;
        }

        static int cmp_mag(const std::vector<uint32_t> &a,
                           const std::vector<uint32_t> &b) {
            if (a.size() != b.size())
                return a.size() < b.size() ? -1 : 1;
            for (size_t i = a.size(); i-- > 0;) {
                if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
            }
            return 0;
        }

        static std::vector<uint32_t> add_mag(const std::vector<uint32_t> &a,
                                             const std::vector<uint32_t> &b) {
            std::vector<uint32_t> r;
            const size_t n = std::max(a.size(), b.size());
            uint64_t carry = 0;
            for (size_t i = 0; i < n; ++i) {
                uint64_t s = carry;
                if (i < a.size()) s += a[i];
                if (i < b.size()) s += b[i];
                r.push_back(static_cast<uint32_t>(s & 0xffffffffu));
                carry = s >> 32;
            }
            if (carry) r.push_back(static_cast<uint32_t>(carry));
            return r;
        }

        // Requires a >= b in magnitude.
        static std::vector<uint32_t> sub_mag(const std::vector<uint32_t> &a,
                                             const std::vector<uint32_t> &b) {
            std::vector<uint32_t> r;
            int64_t borrow = 0;
            for (size_t i = 0; i < a.size(); ++i) {
                int64_t s = static_cast<int64_t>(a[i]) - borrow;
                if (i < b.size()) s -= b[i];
                if (s < 0) {
                    s += (int64_t(1) << 32);
                    borrow = 1;
                } else borrow = 0;
                r.push_back(static_cast<uint32_t>(s));
            }
            while (!r.empty() && r.back() == 0) r.pop_back();
            return r;
        }

        static std::vector<uint32_t> mul_mag(const std::vector<uint32_t> &a,
                                             const std::vector<uint32_t> &b) {
            std::vector<uint64_t> acc(a.size() + b.size(), 0);
            for (size_t i = 0; i < a.size(); ++i) {
                uint64_t carry = 0;
                for (size_t j = 0; j < b.size(); ++j) {
                    const uint64_t cur = acc[i + j] +
                                         static_cast<uint64_t>(a[i]) * b[j] + carry;
                    acc[i + j] = cur & 0xffffffffu;
                    carry = cur >> 32;
                }
                acc[i + b.size()] += carry;
            }
            std::vector<uint32_t> r;
            r.reserve(acc.size());
            for (const uint64_t v: acc) r.push_back(static_cast<uint32_t>(v));
            while (!r.empty() && r.back() == 0) r.pop_back();
            return r;
        }

        // Long division on magnitudes; quotient returned, remainder written
        // to rem. Uses a fast single-limb path (Knuth Algorithm S-style
        // short division) when the divisor is one limb, falling back to
        // bit-at-a-time restoring division otherwise.
        static std::vector<uint32_t> divmod_mag(
            const std::vector<uint32_t> &a, const std::vector<uint32_t> &b,
            std::vector<uint32_t> &rem) {
            rem.clear();
            if (cmp_mag(a, b) < 0) {
                rem = a;
                return {};
            }
            // Fast path: single-limb divisor -> short division, O(n).
            if (b.size() == 1) {
                const uint64_t d = b[0];
                std::vector<uint32_t> q(a.size(), 0);
                uint64_t r = 0;
                for (size_t i = a.size(); i-- > 0;) {
                    const uint64_t cur = (r << 32) | a[i];
                    q[i] = static_cast<uint32_t>(cur / d);
                    r = cur % d;
                }
                while (!q.empty() && q.back() == 0) q.pop_back();
                if (r != 0) rem.push_back(static_cast<uint32_t>(r));
                return q;
            }
            // Bit-at-a-time restoring division. Adequate for Phase 2 sizes.
            std::vector<uint32_t> q(a.size(), 0);
            std::vector<uint32_t> r; // remainder accumulator
            const int total_bits = static_cast<int>(a.size()) * 32;
            for (int i = total_bits - 1; i >= 0; --i) {
                shl1(r);
                if (get_bit(a, i)) set_bit(r, 0);
                if (cmp_mag(r, b) >= 0) {
                    r = sub_mag(r, b);
                    set_bit(q, i);
                }
            }
            while (!q.empty() && q.back() == 0) q.pop_back();
            rem = r;
            return q;
        }

        static bool get_bit(const std::vector<uint32_t> &v, const int i) {
            const size_t limb = i >> 5;
            if (limb >= v.size()) return false;
            return (v[limb] >> (i & 31)) & 1u;
        }

        static void set_bit(std::vector<uint32_t> &v, const int i) {
            const size_t limb = i >> 5;
            if (limb >= v.size()) v.resize(limb + 1, 0);
            v[limb] |= (1u << (i & 31));
        }

        static void shl1(std::vector<uint32_t> &v) {
            uint32_t carry = 0;
            for (size_t i = 0; i < v.size(); ++i) {
                const uint32_t next = (v[i] >> 31) & 1u;
                v[i] = (v[i] << 1) | carry;
                carry = next;
            }
            if (carry) v.push_back(carry);
        }
    };

    // Integer power: base^exp.
    inline BigInt big_pow(const BigInt &base, uint64_t exp) {
        BigInt result(1), b = base;
        while (exp) {
            if (exp & 1) result *= b;
            b *= b;
            exp >>= 1;
        }
        return result;
    }

    // Greatest common divisor of |a| and |b| (non-negative result).
    inline BigInt big_gcd(BigInt a, BigInt b) {
        if (a.negative()) a = -a;
        if (b.negative()) b = -b;
        while (!b.is_zero()) {
            BigInt r;
            BigInt::divmod(a, b, r);
            a = b;
            b = r;
        }
        return a;
    }
} // namespace nam

#endif // NAM_BIG_INT_HPP