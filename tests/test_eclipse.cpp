#include "check.hpp"

#include <cmath>
#include <cstdio>
#include <stdexcept>

#include "abep/constants.hpp"
#include "abep/eclipse.hpp"
#include "abep/orbit.hpp"
#include "abep/units.hpp"

namespace {

using abep::State;
using abep::Vec3;
using abep::units::deg_to_rad;
using namespace abep::constants;

const Vec3 kSun = abep::sun_direction_from_ra_dec(0.0, 0.0);  // +x

// A polar orbit whose node sits at angle `beta` from the Sun direction (+x). Its normal is (sin b, -cos b, 0), so
// the angle between the Sun and the orbit plane is exactly beta, for beta within +-90 degrees.
State polar_orbit(double altitude_m, double beta_rad, double arg_lat_rad) {
    return abep::circular_orbit_state(altitude_m, deg_to_rad(90.0), beta_rad, arg_lat_rad);
}

// Fraction of one orbit in shadow, by testing `samples` evenly spaced points around it.
double sampled_fraction(double altitude_m, double beta_rad, int samples) {
    int inside = 0;
    for (int k = 0; k < samples; ++k) {
        const double theta = 2.0 * abep::units::kPi * (static_cast<double>(k) + 0.5) / static_cast<double>(samples);
        if (abep::in_earth_shadow(polar_orbit(altitude_m, beta_rad, theta).r, kSun)) ++inside;
    }
    return static_cast<double>(inside) / static_cast<double>(samples);
}

}  // namespace

void runEclipseTests() {
    const double R = kEarthEquatorialRadius;

    {
        TEST("shadow: points behind, in front of, beside and on the edge of the cylinder");
        CHECK(abep::in_earth_shadow({-7.0e6, 0.0, 0.0}, kSun));            // behind the Earth, on the axis
        CHECK(!abep::in_earth_shadow({7.0e6, 0.0, 0.0}, kSun));            // on the Sun side
        CHECK(abep::in_earth_shadow({-7.0e6, R - 1.0, 0.0}, kSun));        // 1 m inside the cylinder
        CHECK(!abep::in_earth_shadow({-7.0e6, R + 1.0, 0.0}, kSun));       // 1 m outside
        CHECK(abep::in_earth_shadow({-7.0e6, 0.0, R - 1.0}, kSun));        // the cylinder is round, not a slab
        CHECK(!abep::in_earth_shadow({-7.0e6, R, 0.0}, kSun));             // exactly on the cylinder: sunlit
        CHECK(!abep::in_earth_shadow({0.0, 0.0, 7.0e6}, kSun));            // on the terminator plane: sunlit
        CHECK(!abep::in_earth_shadow({0.0, 1.0, 0.0}, kSun));              // inside the Earth, on the terminator plane
        const Vec3 up = abep::sun_direction_from_ra_dec(0.0, abep::units::kPi / 2.0);
        CHECK(abep::in_earth_shadow({0.0, 0.0, -7.0e6}, {0.0, 0.0, 1.0}));  // Sun along +z, point below
        CHECK(!abep::in_earth_shadow({0.0, 0.0, 7.0e6}, {0.0, 0.0, 1.0}));
        CHECK_NEAR(up.z, 1.0, 1.0e-15);
        CHECK(abep::in_earth_shadow({-7.0e6, 1.0e6, 0.0}, kSun, 2.0e6));   // a smaller shadow radius is honoured
        CHECK(!abep::in_earth_shadow({-7.0e6, 3.0e6, 0.0}, kSun, 2.0e6));
    }
    {
        TEST("sun direction and beta angle");
        // Tolerance fixed before running: 1e-12 rad (a few thousand unit roundoffs of an angle near 0.5).
        // A unit vector from two sines and two cosines is good to a few unit roundoffs (1.1e-16 each); 1e-14 is about 90.
        CHECK_NEAR(abep::norm(abep::sun_direction_from_ra_dec(1.234, 0.456)), 1.0, 1.0e-14);
        CHECK_NEAR(kSun.x, 1.0, 1.0e-15);
        const Vec3 east = abep::sun_direction_from_ra_dec(abep::units::kPi / 2.0, 0.0);
        CHECK_NEAR(east.x, 0.0, 1.0e-15);
        CHECK_NEAR(east.y, 1.0, 1.0e-15);
        CHECK_NEAR(abep::beta_angle(polar_orbit(400.0e3, deg_to_rad(30.0), 0.7), kSun), deg_to_rad(30.0), 1.0e-12);
        CHECK_NEAR(abep::beta_angle(polar_orbit(400.0e3, deg_to_rad(-45.0), 2.0), kSun), deg_to_rad(-45.0), 1.0e-12);
        // An equatorial prograde orbit has its normal along +z, so beta is the Sun's declination.
        const State equatorial = abep::circular_orbit_state(400.0e3, 0.0, 0.0, 1.0);
        CHECK_NEAR(abep::beta_angle(equatorial, abep::sun_direction_from_ra_dec(1.0, deg_to_rad(23.44))), deg_to_rad(23.44), 1.0e-12);
        CHECK_NEAR(abep::beta_angle(equatorial, kSun), 0.0, 1.0e-12);
    }
    {
        TEST("eclipse fraction: the closed form agrees with sampling a circular orbit");
        // Tolerance fixed before running: 1e-4 of an orbit. Sampling 360000 points places each of the two boundaries to
        // within one sample, 2.8e-6 of an orbit, so 1e-4 is 18 times that and still only about 0.5 s of a 92 minute orbit.
        const int samples = 360000;
        struct Case { double altitude_km; double beta_deg; };
        const Case cases[] = {{400.0, 0.0},  {400.0, 30.0}, {400.0, 60.0}, {400.0, 70.0}, {400.0, 75.0},
                              {800.0, 0.0},  {800.0, 45.0}, {35786.0, 0.0}, {35786.0, 5.0}, {35786.0, 9.0}};
        double worst = 0.0;
        for (const Case& c : cases) {
            const double expected = abep::circular_orbit_eclipse_fraction(R + c.altitude_km * 1.0e3, deg_to_rad(c.beta_deg));
            const double measured = sampled_fraction(c.altitude_km * 1.0e3, deg_to_rad(c.beta_deg), samples);
            CHECK_NEAR(measured, expected, 1.0e-4);
            worst = std::fmax(worst, std::fabs(measured - expected));
        }
        std::printf("    worst difference between the closed form and sampling: %.1e of an orbit\n", worst);
        // Above the critical beta angle asin(R / r) there is no eclipse at all, in the formula and in the sampling.
        CHECK_EQ(abep::circular_orbit_eclipse_fraction(R + 400.0e3, deg_to_rad(75.0)), 0.0);
        CHECK_EQ(sampled_fraction(400.0e3, deg_to_rad(75.0), samples), 0.0);
    }
    {
        TEST("eclipse fraction: a propagated orbit spends the predicted share of an orbit in shadow");
        // Tolerance fixed before running: 1e-3 of an orbit. A 1 s step places each boundary to within 1/5554 = 1.8e-4.
        const double altitude = 400.0e3;
        const double a = R + altitude;
        const double period = abep::orbital_period_two_body(a);
        const auto two_body = [](double, const State& s) { return abep::gravity_acceleration(s.r, false); };
        double worst = 0.0;
        for (const double beta_deg : {0.0, 30.0, 60.0}) {
            State s = polar_orbit(altitude, deg_to_rad(beta_deg), 0.0);
            const long long steps = static_cast<long long>(period);  // 1 s steps over one orbit
            long long inside = 0;
            for (long long i = 0; i < steps; ++i) {
                if (abep::in_earth_shadow(s.r, kSun)) ++inside;
                s = abep::rk4_step(s, static_cast<double>(i), 1.0, two_body);
            }
            const double measured = static_cast<double>(inside) / static_cast<double>(steps);
            const double expected = abep::circular_orbit_eclipse_fraction(a, deg_to_rad(beta_deg));
            CHECK_NEAR(measured, expected, 1.0e-3);
            worst = std::fmax(worst, std::fabs(measured - expected));
        }
        std::printf("    worst difference between the closed form and a propagated orbit: %.1e of an orbit\n", worst);
    }
    {
        TEST("eclipse fraction: symmetric in beta, falls with beta and with altitude, and has a critical beta angle");
        const double a = R + 400.0e3;
        CHECK_NEAR(abep::circular_orbit_eclipse_fraction(a, deg_to_rad(30.0)), abep::circular_orbit_eclipse_fraction(a, deg_to_rad(-30.0)), 1.0e-15);
        double previous = abep::circular_orbit_eclipse_fraction(a, 0.0);
        CHECK(previous > 0.38 && previous < 0.40);  // asin(R / r) / pi = 0.3908 for a 400 km orbit
        for (double beta_deg = 5.0; beta_deg <= 70.0; beta_deg += 5.0) {
            const double f = abep::circular_orbit_eclipse_fraction(a, deg_to_rad(beta_deg));
            CHECK(f < previous);
            CHECK(f > 0.0);
            previous = f;
        }
        const double critical = std::asin(R / a);  // 70.2 degrees
        CHECK(abep::circular_orbit_eclipse_fraction(a, critical * 0.999) > 0.0);
        CHECK_EQ(abep::circular_orbit_eclipse_fraction(a, critical * 1.001), 0.0);
        CHECK(abep::circular_orbit_eclipse_fraction(R + 800.0e3, 0.0) < abep::circular_orbit_eclipse_fraction(a, 0.0));
        CHECK_EQ(abep::circular_orbit_eclipse_fraction(a, abep::units::kPi / 2.0), 0.0);
        CHECK_THROWS(abep::circular_orbit_eclipse_fraction(R, 0.0), std::invalid_argument);
        CHECK_THROWS(abep::circular_orbit_eclipse_fraction(R - 1.0, 0.0), std::invalid_argument);
    }
}
