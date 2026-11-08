#pragma once
/**
 * @file test_harness.hpp
 * @brief Minimal dependency-free test harness for the shared C++17 libraries (ADR-006).
 *
 * Usage:
 * @code
 *   TEST_CASE(decodes_rpm) { CHECK_EQ(value, 1726.0); }
 * @endcode
 * Each test executable links tests/unit/test_main.cpp. A test name given on the command line
 * runs only matching tests (substring match). Exit code = number of failed tests.
 */

#include <cstdio>
#include <cstring>
#include <vector>

namespace th {

struct TestCase {
    const char* name;
    void (*function)();
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

inline int& currentFailures() {
    static int failures = 0;
    return failures;
}

struct Registrar {
    Registrar(const char* name, void (*function)()) { registry().push_back({name, function}); }
};

inline void reportFailure(const char* file, int line, const char* expression) {
    std::printf("    FAILED %s:%d: %s\n", file, line, expression);
    ++currentFailures();
}

inline int runAll(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int failedTests = 0;
    int executed = 0;
    for (const TestCase& test : registry()) {
        if (filter != nullptr && std::strstr(test.name, filter) == nullptr) continue;
        currentFailures() = 0;
        test.function();
        ++executed;
        if (currentFailures() == 0) {
            std::printf("PASS %s\n", test.name);
        } else {
            std::printf("FAIL %s\n", test.name);
            ++failedTests;
        }
    }
    std::printf("%d test(s), %d failed\n", executed, failedTests);
    return executed == 0 ? 1 : failedTests;
}

}  // namespace th

#define TH_CONCAT_INNER(a, b) a##b
#define TH_CONCAT(a, b) TH_CONCAT_INNER(a, b)

#define TEST_CASE(name)                                                    \
    static void name();                                                    \
    static const ::th::Registrar TH_CONCAT(registrar_, name)(#name, &name); \
    static void name()

#define CHECK(expression)                                                   \
    do {                                                                    \
        if (!(expression)) ::th::reportFailure(__FILE__, __LINE__, #expression); \
    } while (false)

#define CHECK_EQ(actual, expected) CHECK((actual) == (expected))

/// Like CHECK but stops the current test (use before dereferencing results).
#define REQUIRE(expression)                                                 \
    do {                                                                    \
        if (!(expression)) {                                                \
            ::th::reportFailure(__FILE__, __LINE__, #expression);           \
            return;                                                         \
        }                                                                   \
    } while (false)
