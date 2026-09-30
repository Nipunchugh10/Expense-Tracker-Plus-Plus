#pragma once
// Minimal dependency-free test harness (the project is offline-first, so no
// test framework is downloaded).
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace tf {
struct Case {
    const char* name;
    std::function<void()> fn;
};
inline std::vector<Case>& Registry() {
    static std::vector<Case> cases;
    return cases;
}
inline int& Failures() {
    static int failures = 0;
    return failures;
}
struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { Registry().push_back({name, std::move(fn)}); }
};
struct Abort {};
} // namespace tf

#define TF_CAT2(a, b) a##b
#define TF_CAT(a, b) TF_CAT2(a, b)
#define TEST_CASE(name)                                                                    \
    static void TF_CAT(tf_case_, __LINE__)();                                              \
    static tf::Registrar TF_CAT(tf_reg_, __LINE__)(name, &TF_CAT(tf_case_, __LINE__));     \
    static void TF_CAT(tf_case_, __LINE__)()

#define CHECK(expr)                                                                        \
    do {                                                                                   \
        if (!(expr)) {                                                                     \
            tf::Failures()++;                                                              \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #expr "\n";     \
        }                                                                                  \
    } while (0)

#define REQUIRE(expr)                                                                      \
    do {                                                                                   \
        if (!(expr)) {                                                                     \
            tf::Failures()++;                                                              \
            std::cerr << __FILE__ << ":" << __LINE__ << ": REQUIRE failed: " #expr "\n";   \
            throw tf::Abort{};                                                             \
        }                                                                                  \
    } while (0)

#define CHECK_EQ(a, b)                                                                     \
    do {                                                                                   \
        auto tf_a = (a);                                                                   \
        auto tf_b = (b);                                                                   \
        if (!(tf_a == tf_b)) {                                                             \
            tf::Failures()++;                                                              \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK_EQ failed: " #a " == " #b \
                      << " (" << tf_a << " vs " << tf_b << ")\n";                          \
        }                                                                                  \
    } while (0)
