// tests/test_main.hpp
//
// A tiny zero-dependency test harness so Phase 1 builds without pulling in
// Catch2/GoogleTest. (Those remain the production choice per the plan; this
// keeps the repo self-contained and CI-trivial.)
#ifndef NAM_TEST_MAIN_HPP
#define NAM_TEST_MAIN_HPP

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <functional>

namespace namtest
{
    struct Case
    {
        std::string name;
        std::function<void()> fn;
    };

    inline std::vector<Case>& registry()
    {
        static std::vector<Case> r;
        return r;
    }

    inline int& failures()
    {
        static int f = 0;
        return f;
    }

    struct Reg
    {
        Reg(const char* n, std::function<void()> f)
        {
            registry().push_back({n, std::move(f)});
        }
    };

    inline void check(bool cond, const char* expr, const char* file, int line)
    {
        if (!cond)
        {
            std::printf("  FAIL: %s  (%s:%d)\n", expr, file, line);
            ++failures();
        }
    }
} // namespace namtest

#define NAM_TEST(name) \
    static void name(); \
    static ::namtest::Reg reg_##name(#name, name); \
    static void name()

#define CHECK(cond) ::namtest::check((cond), #cond, __FILE__, __LINE__)

#define NAM_TEST_RUN_ALL() \
    int main() { \
        int total = 0; \
        for (auto& c : ::namtest::registry()) { \
            std::printf("[ RUN ] %s\n", c.name.c_str()); \
            int before = ::namtest::failures(); \
            c.fn(); \
            if (::namtest::failures() == before) \
                std::printf("[ OK  ] %s\n", c.name.c_str()); \
            else \
                std::printf("[FAIL ] %s\n", c.name.c_str()); \
            ++total; \
        } \
        std::printf("\n%d tests, %d failures\n", total, ::namtest::failures()); \
        return ::namtest::failures() == 0 ? 0 : 1; \
    }

#endif // NAM_TEST_MAIN_HPP