#include "check.hpp"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <initializer_list>
#include <vector>

#include "abep/constants.hpp"
#include "abep/orbit.hpp"
#include "abep/units.hpp"

namespace {

using abep::State;
using abep::Vec3;
using namespace abep::constants;
using abep::units::deg_to_rad;

Vec3 two_body(double, const State& s) { return abep::gravity_acceleration(s.r, false); }
Vec3 with_j2(double, const State& s) { return abep::gravity_acceleration(s.r, true); }

double angular_momentum(const State& s) { return abep::norm(abep::cross(s.r, s.v)); }

// Least-squares slope of y against t.
double slope(const std::vector<double>& t, const std::vector<double>& y) {
    const double n = static_cast<double>(t.size());
    double st = 0.0, sy = 0.0, stt = 0.0, sty = 0.0;
    for (std::size_t i = 0; i < t.size(); ++i) {
        st += t[i];
        sy += y[i];
        stt += t[i] * t[i];
        sty += t[i] * y[i];
    }
    return (n * sty - st * sy) / (n * stt - st * st);
}

// Integrate for `days` (10 s step) and return the least-squares rate of the
// unwrapped node angle, rad/s. `j2` = 0 is the point-mass problem. The node is
// sampled every 600 s, so the short-period wobble in the osculating node
// averages out over the many orbits in the run.
double measured_nodal_rate(State s, double j2, double days) {
    const auto accel = [j2](double, const State& st) { return abep::gravity_acceleration(st.r, j2 != 0.0, j2); };
    const double dt = 10.0;
    const double sample = 600.0;
    const long long samples = static_cast<long long>(days * 86400.0 / sample);
    std::vector<double> t{0.0};
    double previous = abep::raan(s);
    std::vector<double> unwrapped{previous};
    for (long long k = 1; k <= samples; ++k) {
        s = abep::propagate(s, static_cast<double>(k - 1) * sample, static_cast<double>(k) * sample, dt, accel);
        const double now = abep::raan(s);
        double step = now - previous;
        if (step > abep::units::kPi) step -= 2.0 * abep::units::kPi;
        if (step < -abep::units::kPi) step += 2.0 * abep::units::kPi;
        unwrapped.push_back(unwrapped.back() + step);
        previous = now;
        t.push_back(static_cast<double>(k) * sample);
    }
    return slope(t, unwrapped);
}

// Height above the ellipsoid along the surface normal (geodetic height), by fixed-point iteration on the
// latitude. Independent of altitude_above_ellipsoid, which uses the geocentric radius instead.
double geodetic_height(const Vec3& r) {
    const double a = kEarthEquatorialRadius;
    const double f = 1.0 / kEarthInverseFlattening;
    const double e2 = 2.0 * f - f * f;
    const double p = std::hypot(r.x, r.y);
    double lat = std::atan2(r.z, p * (1.0 - e2));
    for (int i = 0; i < 100; ++i) {
        const double n = a / std::sqrt(1.0 - e2 * std::sin(lat) * std::sin(lat));
        const double h = p / std::cos(lat) - n;
        lat = std::atan2(r.z, p * (1.0 - e2 * n / (n + h)));
    }
    const double n = a / std::sqrt(1.0 - e2 * std::sin(lat) * std::sin(lat));
    return p / std::cos(lat) - n;
}

}  // namespace

void runOrbitTests() {
    {
        TEST("altitude: above the ellipsoid, not above the equatorial radius");
        // Tolerances fixed before running: 1e-6 m for points built to lie on, or at a known height
        // above, the ellipsoid; 5 m against geodetic height, from a numerical look at latitudes 0 to
        // 90 deg and heights 200 to 800 km (research notes in docs/VALIDATION.md).
        const double a = kEarthEquatorialRadius;
        const double b = a * (1.0 - 1.0 / kEarthInverseFlattening);
        CHECK_NEAR(abep::altitude_above_ellipsoid({a + 400.0e3, 0.0, 0.0}), 400.0e3, 1.0e-6);
        CHECK_NEAR(abep::altitude_above_ellipsoid({0.0, 0.0, b + 400.0e3}), 400.0e3, 1.0e-6);
        // A point on the surface, from the parametric form (a cos t, b sin t), has altitude zero.
        for (double t = 0.0; t < 6.28; t += 0.37) {
            CHECK_NEAR(abep::altitude_above_ellipsoid({a * std::cos(t), 0.0, b * std::sin(t)}), 0.0, 1.0e-6);
        }
        // The naive altitude is low by a - b = 21.4 km at the pole.
        const Vec3 pole{0.0, 0.0, b + 400.0e3};
        CHECK_NEAR(abep::norm(pole) - a - abep::altitude_above_ellipsoid(pole), -(a - b), 1.0e-6);
        // Close to the true geodetic height everywhere on a LEO shell.
        for (double lat_deg = 0.0; lat_deg <= 90.0; lat_deg += 7.5) {
            for (double h : {200.0e3, 400.0e3, 800.0e3}) {
                const double lat = deg_to_rad(lat_deg);
                const double f = 1.0 / kEarthInverseFlattening;
                const double e2 = 2.0 * f - f * f;
                const double n = a / std::sqrt(1.0 - e2 * std::sin(lat) * std::sin(lat));
                const Vec3 r{(n + h) * std::cos(lat), 0.0, (n * (1.0 - e2) + h) * std::sin(lat)};
                CHECK_NEAR(geodetic_height(r), h, 1.0e-6);
                CHECK_NEAR(abep::altitude_above_ellipsoid(r), geodetic_height(r), 5.0);
            }
        }
    }
    {
        TEST("gravity: J2 term at the equator and at the pole");
        // Fixed before running. At the equator the J2 term is inward, -(3/2) J2 GM Re^2 / r^4;
        // at the pole it is outward and twice as large, +3 J2 GM Re^2 / r^4 (an oblate Earth
        // attracts less strongly along its axis than in its equatorial plane, at equal radius).
        const double r0 = kEarthEquatorialRadius + 400.0e3;
        const double j2_scale = kEarthJ2 * kEarthGM * kEarthEquatorialRadius * kEarthEquatorialRadius / std::pow(r0, 4);

        const Vec3 equator{r0, 0.0, 0.0};
        const double dx = abep::gravity_acceleration(equator, true).x - abep::gravity_acceleration(equator, false).x;
        CHECK_NEAR(dx, -1.5 * j2_scale, 1.0e-13);
        CHECK_NEAR(abep::gravity_acceleration(equator, false).x, -kEarthGM / (r0 * r0), 1.0e-12);

        const Vec3 pole{0.0, 0.0, r0};
        const double dz = abep::gravity_acceleration(pole, true).z - abep::gravity_acceleration(pole, false).z;
        CHECK_NEAR(dz, 3.0 * j2_scale, 1.0e-13);
    }
    {
        TEST("circular_orbit_state: elements are recovered");
        const double h = 550.0e3;
        const State s = abep::circular_orbit_state(h, deg_to_rad(53.0), deg_to_rad(120.0), deg_to_rad(40.0));
        CHECK_NEAR(abep::norm(s.r), kEarthEquatorialRadius + h, 1.0e-6);
        CHECK_NEAR(abep::norm(s.v), std::sqrt(kEarthGM / (kEarthEquatorialRadius + h)), 1.0e-9);
        // r . v is zero on a circular orbit, but a dot product of two vectors this long is only good to a few unit
        // roundoffs of |r||v| (one is 5.8e-6 m^2/s here), so the tolerance is relative: 1e-13 of |r||v|, the same
        // relative scale as the length checks around it. An absolute 1e-6 sat below that noise and passed on one
        // toolchain only by luck (docs/VALIDATION.md, finding 9).
        CHECK_NEAR(abep::dot(s.r, s.v), 0.0, 1.0e-13 * abep::norm(s.r) * abep::norm(s.v));
        CHECK_NEAR(abep::inclination(s), deg_to_rad(53.0), 1.0e-12);
        CHECK_NEAR(abep::raan(s), deg_to_rad(120.0), 1.0e-12);
        CHECK_NEAR(abep::semi_major_axis(s), kEarthEquatorialRadius + h, 1.0e-6);
    }
    {
        TEST("two-body: a circular orbit returns to its start after one Keplerian period");
        // Tolerances fixed before running: 1 mm and 1 um/s after one orbit with a 1 s step; 1e-12
        // relative drift in energy and angular momentum.
        const double a = kEarthEquatorialRadius + 400.0e3;
        const State s0 = abep::circular_orbit_state(400.0e3, deg_to_rad(51.6), deg_to_rad(30.0), deg_to_rad(10.0));
        const double period = abep::orbital_period_two_body(a);
        const State s1 = abep::propagate(s0, 0.0, period, 1.0, two_body);
        CHECK_NEAR(abep::norm(s1.r - s0.r), 0.0, 1.0e-3);
        CHECK_NEAR(abep::norm(s1.v - s0.v), 0.0, 1.0e-6);
        CHECK_NEAR(abep::specific_energy(s1) / abep::specific_energy(s0), 1.0, 1.0e-12);
        CHECK_NEAR(angular_momentum(s1) / angular_momentum(s0), 1.0, 1.0e-12);
    }
    {
        TEST("two-body: an eccentric orbit returns to its start after one Keplerian period");
        // Tolerances fixed before running: 1 cm and 10 um/s. Starts at perigee, a = 12000 km, e = 0.3.
        const double a = 12.0e6;
        const double e = 0.3;
        const State s0{{a * (1.0 - e), 0.0, 0.0}, {0.0, std::sqrt(kEarthGM / a * (1.0 + e) / (1.0 - e)), 0.0}};
        const double period = abep::orbital_period_two_body(a);
        const State s1 = abep::propagate(s0, 0.0, period, 1.0, two_body);
        CHECK_NEAR(abep::norm(s1.r - s0.r), 0.0, 1.0e-2);
        CHECK_NEAR(abep::norm(s1.v - s0.v), 0.0, 1.0e-5);
        CHECK_NEAR(abep::semi_major_axis(s1) / a, 1.0, 1.0e-12);
    }
    {
        TEST("two-body: the node does not move without J2");
        // Tolerance fixed before running: 1e-9 rad over 10 days.
        const State s0 = abep::circular_orbit_state(400.0e3, deg_to_rad(51.6), deg_to_rad(30.0));
        CHECK_NEAR(measured_nodal_rate(s0, 0.0, 10.0) * 10.0 * 86400.0, 0.0, 1.0e-9);
    }
    {
        TEST("J2: nodal regression of a prograde orbit matches the analytic rate");
        // The analytic rate is first order in J2, so the propagated rate is expected to differ from it
        // by a relative amount of order J2 itself (about 1e-3, times an order-one coefficient). Two
        // checks, fixed before running:
        //   1. the difference is under 0.5% at Earth's J2;
        //   2. it shrinks as J2 shrinks: at J2/10 it is less than an eighth of what it was at J2.
        // The second one is what separates "a neglected second-order term" from "a wrong coefficient",
        // which would not go away as J2 does.
        const double h = 400.0e3;
        const double a = kEarthEquatorialRadius + h;
        const double inc = deg_to_rad(51.6);
        const State s0 = abep::circular_orbit_state(h, inc, deg_to_rad(30.0));

        const double analytic = abep::j2_nodal_rate(a, 0.0, inc);
        const double relative = measured_nodal_rate(s0, kEarthJ2, 10.0) / analytic - 1.0;
        const double small_j2 = kEarthJ2 / 10.0;
        const double relative_small =
            measured_nodal_rate(s0, small_j2, 10.0) / abep::j2_nodal_rate(a, 0.0, inc, small_j2) - 1.0;

        std::printf("    prograde 400 km, 51.6 deg: %.4f deg/day analytic, propagated differs by %+.4f%%\n",
                    abep::units::rad_to_deg(analytic) * 86400.0, 100.0 * relative);
        std::printf("    with J2 / 10: differs by %+.4f%%\n", 100.0 * relative_small);
        CHECK(analytic < 0.0);
        CHECK(std::fabs(relative) < 5.0e-3);
        CHECK(std::fabs(relative_small) < std::fabs(relative) / 8.0);
    }
    {
        TEST("J2: a retrograde orbit precesses, and the sun-synchronous rate comes out right");
        // Tolerances fixed before running: the analytic rate at 800 km and 98.6 deg against the
        // sun-synchronous rate 2*pi / (365.2422 d), 0.5% (98.6 deg is the usual rounded inclination
        // for 800 km, which sets the limit); the propagated rate against the analytic one, 0.5%
        // (same second-order J2 effect as above).
        const double h = 800.0e3;
        const double a = kEarthEquatorialRadius + h;
        const double inc = deg_to_rad(98.6);
        const double sun_sync = 2.0 * abep::units::kPi / (365.2422 * 86400.0);
        const double analytic = abep::j2_nodal_rate(a, 0.0, inc);
        const double measured = measured_nodal_rate(abep::circular_orbit_state(h, inc, deg_to_rad(30.0)), kEarthJ2, 10.0);
        std::printf("    retrograde 800 km, 98.6 deg: %.5f deg/day analytic, %.5f propagated, %.5f sun-synchronous\n",
                    abep::units::rad_to_deg(analytic) * 86400.0, abep::units::rad_to_deg(measured) * 86400.0,
                    abep::units::rad_to_deg(sun_sync) * 86400.0);
        CHECK(analytic > 0.0);
        CHECK(measured > 0.0);
        CHECK(std::fabs(analytic / sun_sync - 1.0) < 5.0e-3);
        CHECK(std::fabs(measured / analytic - 1.0) < 5.0e-3);
    }
    {
        TEST("J2: a polar orbit's node does not move");
        // Tolerance fixed before running: 1e-6 rad over 10 days. cos(90 deg) = 0, so J2 leaves the
        // node alone; this is a sign and symmetry check on the J2 acceleration.
        const State s0 = abep::circular_orbit_state(600.0e3, deg_to_rad(90.0), deg_to_rad(30.0));
        CHECK_NEAR(measured_nodal_rate(s0, kEarthJ2, 10.0) * 10.0 * 86400.0, 0.0, 1.0e-6);
    }
    {
        TEST("J2: the z component of angular momentum is conserved");
        // J2 is axisymmetric about the polar axis, so h_z is conserved exactly by the true motion.
        // Tolerance fixed before running: 1e-10 relative over a day. The step is 2 s: a first attempt
        // with 10 s drifted by about 2e-10, which is RK4's own amplitude error, (n dt)^6 / 144 per
        // step, and not a property of J2 (see docs/VALIDATION.md).
        const State s0 = abep::circular_orbit_state(500.0e3, deg_to_rad(60.0), deg_to_rad(10.0), deg_to_rad(20.0));
        const State s1 = abep::propagate(s0, 0.0, 86400.0, 2.0, with_j2);
        const double hz0 = abep::cross(s0.r, s0.v).z;
        const double hz1 = abep::cross(s1.r, s1.v).z;
        CHECK_NEAR(hz1 / hz0, 1.0, 1.0e-10);
    }
}
