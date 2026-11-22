#pragma once

// The whole test framework. No external dependency: a simulator that needs a
// package manager to test gets tested by fewer of the people who should be able
// to check it.

#include <cmath>
#include <cstdio>

namespace check {

inline int& checks() { static int n = 0; return n; }
inline int& failures() { static int n = 0; return n; }
inline const char*& current() { static const char* name = ""; return name; }

inline void record(bool ok, const char* expression, const char* file, int line) {
    ++checks();
    if (!ok) {
        ++failures();
        std::printf("  FAIL  %s\n        %s:%d  %s\n", current(), file, line, expression);
    }
}

struct Scope {
    explicit Scope(const char* name) {
        current() = name;
        std::printf("  %s\n", name);
    }
};

}  // namespace check

#define CHECK(expression) check::record((expression), #expression, __FILE__, __LINE__)
#define CHECK_EQ(a, b) check::record((a) == (b), #a " == " #b, __FILE__, __LINE__)
// Absolute tolerance. Physics tests state their tolerance in the test, up front.
#define CHECK_NEAR(a, b, tol) \
    check::record(std::fabs((a) - (b)) <= (tol), "|" #a " - " #b "| <= " #tol, __FILE__, __LINE__)
// The expression must throw exactly this kind of exception.
#define CHECK_THROWS(expression, exception_type)                                                     \
    do {                                                                                              \
        bool thrown_ = false;                                                                         \
        try {                                                                                         \
            (void)(expression);                                                                       \
        } catch (const exception_type&) {                                                             \
            thrown_ = true;                                                                           \
        }                                                                                             \
        check::record(thrown_, #expression " throws " #exception_type, __FILE__, __LINE__);          \
    } while (false)
#define TEST(name) check::Scope test_scope_(name)

void runAtmosphereTests();
void runConstantsTests();
void runCsvTests();
void runDragTests();
void runEclipseTests();
void runFlightTests();
void runMissionTests();
void runOrbitTests();
void runPowerTests();
void runRomano2018Tests();
void runRunConfigTests();
void runStandardAtmosphere1976Tests();
void runSummaryTests();
void runThermalTests();
void runThrustTests();
void runUnitsTests();
void runVersionTests();
