#include "check.hpp"

#include <cmath>

#include "abep/constants.hpp"

namespace {

// J2 of the level ellipsoid defined by (a, 1/f, GM, omega): the second-degree
// zonal harmonic of its normal gravity field,
//   J2 = (e^2 / 3) * (1 - (2/15) * m * e' / q0),
// with e^2 = 2f - f^2, m = omega^2 a^2 b / GM, e' = sqrt(a^2 - b^2) / b and
// q0 = 1/2 * ((1 + 3/e'^2) * atan(e') - 3/e').
//
// Evaluating q0 as written subtracts two numbers near 36.5 to leave about 7e-5,
// which loses five digits. Its series in e' has no such cancellation:
//   q0 = sum_{k>=1} (-1)^(k+1) * 2k * e'^(2k+1) / ((2k+1)(2k+3)),
// which converges fast because e' is about 0.08.
double j2_from_defining_parameters() {
    using namespace abep::constants;
    const double a = kEarthEquatorialRadius;
    const double f = 1.0 / kEarthInverseFlattening;
    const double b = a * (1.0 - f);
    const double e2 = 2.0 * f - f * f;
    const double ep = std::sqrt(a * a - b * b) / b;
    const double m = kEarthRotationRate * kEarthRotationRate * a * a * b / kEarthGM;
    double q0 = 0.0;
    double power = ep * ep * ep;  // e'^(2k+1), starting at k = 1
    for (int k = 1; k <= 12; ++k) {
        const double kk = static_cast<double>(k);
        q0 += ((k % 2 == 1) ? 1.0 : -1.0) * 2.0 * kk * power / ((2.0 * kk + 1.0) * (2.0 * kk + 3.0));
        power *= ep * ep;
    }
    return (e2 / 3.0) * (1.0 - (2.0 / 15.0) * m * ep / q0);
}

}  // namespace

void runConstantsTests() {
    {
        TEST("J2 is derived from the WGS 84 defining parameters");
        // Tolerance fixed before the comparison: 1e-15 absolute on a value of about 1e-3
        // (relative 1e-12). A first attempt evaluated q0 directly and failed this: see
        // docs/VALIDATION.md. The series above has no cancellation.
        CHECK_NEAR(abep::constants::kEarthJ2, j2_from_defining_parameters(), 1.0e-15);
    }
    {
        TEST("J2 agrees with the published ellipsoid second-degree coefficient");
        // The normalized geometric coefficient C2,0 = -4.84166774985e-4 is printed in the AHRS
        // documentation of WGS 84, and J2 = -sqrt(5) * C2,0. The page gives 12 digits, so 2e-15
        // absolute.
        CHECK_NEAR(abep::constants::kEarthJ2, std::sqrt(5.0) * 4.84166774985e-4, 2.0e-15);
    }
    {
        TEST("defining parameters keep their published values");
        // Guards against an accidental edit: these are the WGS 84 defining values.
        CHECK_EQ(abep::constants::kEarthEquatorialRadius, 6378137.0);
        CHECK_EQ(abep::constants::kEarthInverseFlattening, 298.257223563);
        CHECK_EQ(abep::constants::kEarthGM, 3.986004418e14);
        CHECK_EQ(abep::constants::kEarthRotationRate, 7.292115e-5);
    }
}
