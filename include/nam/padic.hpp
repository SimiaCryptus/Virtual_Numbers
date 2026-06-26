// include/nam/padic.hpp
//
// M5: p-adic numbers (Section 3.4). Digit commitment is LOCAL -- the easy
// win. We index from the least-significant digit upward, with digits in
// {0,...,p-1}.
//
// A p-adic rational a/b (with gcd(b,p)=1) has an ultimately periodic
// expansion => finite state space => periodic orbit => skip-ahead for free.
//
// The step recurrence for x = a/b in Z_p (b invertible mod p):
//   digit d = (a * b^{-1}) mod p
//   a' = (a - d*b) / p          (exact integer division -- locality)
//
// AutomatonVM layout:
//   base    : the prime p (codec)
//   phase   : digit index (LSB-up)
//   state[0]: current numerator a (can be negative; stored as int64 bits)
//   state[1]: denominator b
#ifndef NAM_PADIC_HPP
#define NAM_PADIC_HPP

#include <cstdint>
#include <cstdlib>

#include "nam/abi.h"
#include "nam/generator.hpp"

namespace nam {

namespace detail {

// Modular inverse of b mod p (p prime, gcd(b,p)=1), via extended Euclid.
inline int64_t mod_inverse(int64_t b, int64_t p) {
    int64_t t = 0, newt = 1;
    int64_t r = p, newr = ((b % p) + p) % p;
    while (newr != 0) {
        int64_t q = r / newr;
        int64_t tmp = t - q * newt; t = newt; newt = tmp;
        tmp = r - q * newr; r = newr; newr = tmp;
    }
    if (r > 1) return 0; // not invertible
    if (t < 0) t += p;
    return t;
}

inline int64_t floordiv(int64_t a, int64_t b) {
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

} // namespace detail

struct PAdic {
    static NumVMStep step(AutomatonVM s) {
        int64_t a = static_cast<int64_t>(s.state[0]);
        int64_t b = static_cast<int64_t>(s.state[1]);
        int64_t p = static_cast<int64_t>(s.base);

        int64_t binv = detail::mod_inverse(b, p);
        int64_t d = ((a % p) * binv) % p;
        d = ((d % p) + p) % p;

        // a' = (a - d*b) / p  -- exact (a - d*b is divisible by p).
        int64_t numerator = a - d * b;
        int64_t na = numerator / p;

        AutomatonVM next = s;
        next.state[0] = static_cast<uint64_t>(na);
        next.phase = s.phase + 1;
        return NumVMStep{static_cast<uint32_t>(d), next};
    }
};

static_assert(Generator<PAdic>);

// Build the p-adic generator for a/b in Z_p. Requires gcd(b,p) == 1.
inline AutomatonVM make_padic(int64_t a, int64_t b, uint32_t p) {
    AutomatonVM vm{};
    vm.base = p;
    vm.phase = 0;
    vm.state[0] = static_cast<uint64_t>(a);
    vm.state[1] = static_cast<uint64_t>(b);
    return vm;
}

// Valuation extractor (Section: Valuation Extractor). v_p(n) for an integer
// n: the largest k with p^k | n. For a/b with gcd(b,p)=1 this is v_p(a).
inline int64_t p_valuation(int64_t n, int64_t p) {
    if (n == 0) return -1; // conventionally +infinity; sentinel here
    int64_t v = 0;
    while (n % p == 0) { n /= p; ++v; }
    return v;
}

// Period detection over the (a,b) state: since b is fixed and a is reduced,
// the orbit is finite when |a| is bounded by |b|.
inline std::pair<uint64_t, uint64_t> padic_period(AutomatonVM vm) {
    std::vector<std::pair<int64_t, uint64_t>> seen;
    uint64_t idx = 0;
    while (true) {
        int64_t a = static_cast<int64_t>(vm.state[0]);
        for (auto& [aa, i] : seen) {
            if (aa == a) return {i, idx - i};
        }
        seen.emplace_back(a, idx);
        vm = PAdic::step(vm).next;
        ++idx;
        if (idx > 1'000'000) return {idx, 0};
    }
}

} // namespace nam

#endif // NAM_PADIC_HPP