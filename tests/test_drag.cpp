#include "check.hpp"

#include <cmath>
#include <cstdio>

#include "abep/constants.hpp"
#include "abep/drag.hpp"
#include "abep/orbit.hpp"
#include "abep/units.hpp"

void runDragTests() {
    using abep::State;
    using abep::Vec3;
    using namespace abep::constants;

    // Illustrative numbers for the arithmetic checks; not claims about any real satellite or altitude.
    const double rho = 1.0e-11;  // kg/m^3
    const double cd = 2.2;
    const double area = 1.0;     // m^2
    const double mass = 100.0;   // kg
    const double ballistic = cd * area / mass;

    {
        TEST("drag: magnitude and direction in a non-rotating atmosphere");
        // Circular equatorial orbit, velocity along +y: drag is -1/2 rho (Cd A / m) v^2 along y.
        // Tolerance fixed before running: 1e-18 m/s^2 on a value of about 6e-6.
        const double r0 = kEarthEquatorialRadius + 400.0e3;
        const double v = std::sqrt(kEarthGM / r0);
        const State s{{r0, 0.0, 0.0}, {0.0, v, 0.0}};
        const Vec3 a = abep::drag_acceleration(s, rho, cd, area, mass, false);
        CHECK_NEAR(a.x, 0.0, 1.0e-18);
        CHECK_NEAR(a.y, -0.5 * rho * ballistic * v * v, 1.0e-18);
        CHECK_NEAR(a.z, 0.0, 1.0e-18);
    }
    {
        TEST("drag: a co-rotating atmosphere lowers the relative speed of a prograde satellite");
        // At the equator the air moves at omega * r along +y, so the relative speed is v - omega * r.
        const double r0 = kEarthEquatorialRadius + 400.0e3;
        const double v = std::sqrt(kEarthGM / r0);
        const State s{{r0, 0.0, 0.0}, {0.0, v, 0.0}};
        const double v_rel = v - kEarthRotationRate * r0;
        const Vec3 a = abep::drag_acceleration(s, rho, cd, area, mass, true);
        CHECK_NEAR(a.y, -0.5 * rho * ballistic * v_rel * v_rel, 1.0e-18);
        CHECK(std::fabs(a.y) < std::fabs(abep::drag_acceleration(s, rho, cd, area, mass, false).y));
        // A retrograde satellite meets the air faster, so its drag is larger.
        const State retro{{r0, 0.0, 0.0}, {0.0, -v, 0.0}};
        CHECK(std::fabs(abep::drag_acceleration(retro, rho, cd, area, mass, true).y) >
              std::fabs(abep::drag_acceleration(retro, rho, cd, area, mass, false).y));
    }
    {
        TEST("drag: opposes the relative velocity, and vanishes with the air");
        const State s = abep::circular_orbit_state(400.0e3, abep::units::deg_to_rad(51.6), 1.0, 2.0);
        const Vec3 v_rel = abep::velocity_relative_to_air(s, true);
        const Vec3 a = abep::drag_acceleration(s, rho, cd, area, mass, true);
        CHECK(abep::dot(a, v_rel) < 0.0);
        CHECK_NEAR(abep::norm(abep::cross(a, v_rel)) / (abep::norm(a) * abep::norm(v_rel)), 0.0, 1.0e-12);
        const Vec3 none = abep::drag_acceleration(s, 0.0, cd, area, mass, true);
        CHECK_EQ(none.x, 0.0);
        CHECK_EQ(none.y, 0.0);
        CHECK_EQ(none.z, 0.0);
    }
    {
        TEST("drag: decay per revolution matches the classical result");
        // For a circular orbit in a non-rotating atmosphere of constant density, the semi-major axis
        // shrinks by 2*pi * rho * (Cd A / m) * a^2 per revolution. Tolerance fixed before running:
        // 0.1%. The formula neglects the change of a during the revolution and the eccentricity that
        // drag induces, both of order delta_a / a, about 1e-5 here.
        const double h = 400.0e3;
        const double a0 = kEarthEquatorialRadius + h;
        const State s0 = abep::circular_orbit_state(h, abep::units::deg_to_rad(51.6), 0.5, 1.0);
        const auto accel = [&](double, const State& st) {
            return abep::gravity_acceleration(st.r, false) + abep::drag_acceleration(st, rho, cd, area, mass, false);
        };
        const State s1 = abep::propagate(s0, 0.0, abep::orbital_period_two_body(a0), 1.0, accel);
        const double measured = abep::semi_major_axis(s1) - abep::semi_major_axis(s0);
        const double expected = -2.0 * abep::units::kPi * rho * ballistic * a0 * a0;
        std::printf("    decay per revolution: %.4f m expected, %.4f m measured (%+.4f%%)\n", expected, measured,
                    100.0 * (measured / expected - 1.0));
        CHECK(measured < 0.0);
        CHECK(std::fabs(measured / expected - 1.0) < 1.0e-3);
    }
}
