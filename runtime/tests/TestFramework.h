#pragma once
// Minimal self-contained test framework -- there is no test runner
// already in this project, and pulling in a third-party one (Catch2,
// GoogleTest) isn't available without a package manager in this build
// environment. Deliberately small: TEST_CASE + a handful of ASSERT_*
// macros is enough for the runtime's needs.
#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace mzzplork::test {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& Registry() {
    static std::vector<TestCase> registry;
    return registry;
}

struct Registrar {
    Registrar(const std::string& name, std::function<void()> fn) {
        Registry().push_back(TestCase{name, std::move(fn)});
    }
};

struct AssertionFailure {
    std::string message;
};

inline int RunAll() {
    int failed = 0;
    for (auto& test : Registry()) {
        try {
            test.fn();
            std::cout << "  [PASS] " << test.name << "\n";
        } catch (const AssertionFailure& failure) {
            std::cout << "  [FAIL] " << test.name << " -- " << failure.message << "\n";
            failed++;
        } catch (const std::exception& e) {
            std::cout << "  [FAIL] " << test.name << " -- unexpected exception: " << e.what() << "\n";
            failed++;
        } catch (...) {
            std::cout << "  [FAIL] " << test.name << " -- unexpected non-std exception\n";
            failed++;
        }
    }
    std::cout << (Registry().size() - failed) << "/" << Registry().size() << " tests passed\n";
    return failed == 0 ? 0 : 1;
}

}  // namespace mzzplork::test

#define MZZPLORK_CONCAT_INNER(a, b) a##b
#define MZZPLORK_CONCAT(a, b) MZZPLORK_CONCAT_INNER(a, b)

#define TEST_CASE(name)                                                                        \
    static void MZZPLORK_CONCAT(test_fn_, name)();                                              \
    static ::mzzplork::test::Registrar MZZPLORK_CONCAT(test_registrar_, name)(                   \
        #name, MZZPLORK_CONCAT(test_fn_, name));                                                 \
    static void MZZPLORK_CONCAT(test_fn_, name)()

#define ASSERT_TRUE(cond)                                                                       \
    do {                                                                                        \
        if (!(cond)) {                                                                          \
            std::ostringstream oss;                                                             \
            oss << "ASSERT_TRUE(" #cond ") failed at " __FILE__ ":" << __LINE__;                \
            throw ::mzzplork::test::AssertionFailure{oss.str()};                                \
        }                                                                                        \
    } while (0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))

#define ASSERT_EQ(a, b)                                                                         \
    do {                                                                                        \
        if (!((a) == (b))) {                                                                    \
            std::ostringstream oss;                                                             \
            oss << "ASSERT_EQ(" #a ", " #b ") failed at " __FILE__ ":" << __LINE__               \
                << " -- got \"" << (a) << "\" vs \"" << (b) << "\"";                             \
            throw ::mzzplork::test::AssertionFailure{oss.str()};                                \
        }                                                                                        \
    } while (0)
