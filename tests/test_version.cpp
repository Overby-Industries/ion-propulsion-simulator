#include "check.hpp"

#include <cstring>

#include "abep/version.hpp"

void runVersionTests() {
    TEST("version is set and looks like major.minor.patch");
    const char* v = abep::version();
    CHECK(v != nullptr);
    CHECK(std::strlen(v) >= 5);
    int dots = 0;
    for (const char* p = v; *p != '\0'; ++p) {
        if (*p == '.') ++dots;
    }
    CHECK_EQ(dots, 2);
}
