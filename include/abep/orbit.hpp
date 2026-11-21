#pragma once

// Orbit propagation: two-body gravity plus the J2 oblateness term, integrated
// with fixed-step RK4. Earth-centred inertial frame, SI units. Deterministic:
// no adaptive steps, no random numbers.

#include <algorithm>
#include <cmath>

#include "abep/constants.hpp"
#include "abep/units.hpp"
#include "abep/vec3.hpp"

namespace abep {

struct State {
    Vec3 r;  // position, m
    Vec3 v;  // velocity, m/s
};

// Gravitational acceleration at position r: the point-mass term, plus the J2
// perturbation if requested. The J2 term is
//   -(3/2) J2 GM Re^2 / r^5 * [ x(1 - 5z^2/r^2), y(1 - 5z^2/r^2), z(3 - 5z^2/r^2) ].
// `j2` defaults to Earth's value; the tests scale it to check how the result
// depends on it.
inline Vec3 gravity_acceleration(const Vec3& r, bool include_j2, double j2 = constants::kEarthJ2) noexcept {
    using namespace constants;
    const double r2 = dot(r, r);
    const double rn = std::sqrt(r2);
    Vec3 a = (-kEarthGM / (r2 * rn)) * r;
    if (include_j2) {
        const double k = 1.5 * j2 * kEarthGM * kEarthEquatorialRadius * kEarthEquatorialRadius /
                         (r2 * r2 * rn);
        const double z2_over_r2 = r.z * r.z / r2;
        a.x += k * r.x * (5.0 * z2_over_r2 - 1.0);
        a.y += k * r.y * (5.0 * z2_over_r2 - 1.0);
        a.z += k * r.z * (5.0 * z2_over_r2 - 3.0);
    }
    return a;
}

namespace detail {

inline State advanced(const State& s, const State& rate, double h) noexcept {
    return {s.r + h * rate.r, s.v + h * rate.v};
}

}  // namespace detail

// One RK4 step of length dt. `accel(t, state)` returns the total acceleration
// (gravity, drag, thrust, ...) as a Vec3.
template <class Accel>
State rk4_step(const State& s, double t, double dt, const Accel& accel) {
    const auto rate = [&accel](double tt, const State& st) { return State{st.v, accel(tt, st)}; };
    const State k1 = rate(t, s);
    const State k2 = rate(t + 0.5 * dt, detail::advanced(s, k1, 0.5 * dt));
    const State k3 = rate(t + 0.5 * dt, detail::advanced(s, k2, 0.5 * dt));
    const State k4 = rate(t + dt, detail::advanced(s, k3, dt));
    return {s.r + (dt / 6.0) * (k1.r + 2.0 * k2.r + 2.0 * k3.r + k4.r),
            s.v + (dt / 6.0) * (k1.v + 2.0 * k2.v + 2.0 * k3.v + k4.v)};
}

// Integrate from t0 to t1 in steps of dt, with a shorter final step so that the
// run ends exactly at t1.
template <class Accel>
State propagate(State s, double t0, double t1, double dt, const Accel& accel) {
    const double span = t1 - t0;
    // long long: `long` is 32 bits on Windows, which a long run at a small step could overflow.
    const long long whole_steps = static_cast<long long>(span / dt);
    for (long long i = 0; i < whole_steps; ++i) {
        s = rk4_step(s, t0 + static_cast<double>(i) * dt, dt, accel);
    }
    const double rest = span - static_cast<double>(whole_steps) * dt;
    if (rest > 0.0) {
        s = rk4_step(s, t0 + static_cast<double>(whole_steps) * dt, rest, accel);
    }
    return s;
}

// State on a circular orbit of the given altitude above the equatorial radius.
// `arg_lat_rad` is the argument of latitude: the angle from the ascending node
// along the orbit. Speed is the circular two-body speed sqrt(GM / r).
inline State circular_orbit_state(double altitude_m, double inclination_rad, double raan_rad = 0.0,
                                  double arg_lat_rad = 0.0) noexcept {
    const double radius = constants::kEarthEquatorialRadius + altitude_m;
    const double speed = std::sqrt(constants::kEarthGM / radius);
    const Vec3 node{std::cos(raan_rad), std::sin(raan_rad), 0.0};  // towards the ascending node
    const Vec3 ahead{-std::sin(raan_rad) * std::cos(inclination_rad),
                     std::cos(raan_rad) * std::cos(inclination_rad), std::sin(inclination_rad)};
    const double c = std::cos(arg_lat_rad);
    const double s = std::sin(arg_lat_rad);
    return {radius * (c * node + s * ahead), speed * (c * ahead - s * node)};
}

// Altitude above the WGS 84 ellipsoid, measured along the geocentric radius, m.
// Use this, not |r| - Re, for the density lookup: against the equatorial radius
// the naive form is wrong by up to 21 km at the poles (13 km at 51.6 deg
// latitude), which changes density by tens of percent. This radial form differs
// from true geodetic height by under 5 m at LEO altitudes (checked in
// tests/test_orbit.cpp).
inline double altitude_above_ellipsoid(const Vec3& r) noexcept {
    const double a = constants::kEarthEquatorialRadius;
    const double b = a * (1.0 - 1.0 / constants::kEarthInverseFlattening);
    const double rn = norm(r);
    const double s = r.z / rn;  // sine of the geocentric latitude
    const double ellipsoid_radius = a * b / std::sqrt(b * b * (1.0 - s * s) + a * a * s * s);
    return rn - ellipsoid_radius;
}

// Specific orbital energy, v^2/2 - GM/r (J/kg). Conserved under two-body motion.
inline double specific_energy(const State& s) noexcept {
    return 0.5 * dot(s.v, s.v) - constants::kEarthGM / norm(s.r);
}

// Semi-major axis from the energy (vis-viva), m.
inline double semi_major_axis(const State& s) noexcept {
    return -constants::kEarthGM / (2.0 * specific_energy(s));
}

// Right ascension of the ascending node, rad, in (-pi, pi]. Undefined for an
// equatorial orbit.
inline double raan(const State& s) noexcept {
    const Vec3 h = cross(s.r, s.v);
    return std::atan2(h.x, -h.y);
}

// Inclination, rad.
inline double inclination(const State& s) noexcept {
    const Vec3 h = cross(s.r, s.v);
    return std::acos(h.z / norm(h));
}

// Keplerian period 2*pi*sqrt(a^3 / GM), s. The exact two-body answer.
inline double orbital_period_two_body(double semi_major_axis_m) noexcept {
    const double a = semi_major_axis_m;
    return 2.0 * units::kPi * std::sqrt(a * a * a / constants::kEarthGM);
}

// Secular rate of the node due to J2, rad/s (first order in J2):
//   dOmega/dt = -(3/2) J2 (Re/p)^2 n cos(i),  p = a(1 - e^2),  n = sqrt(GM/a^3).
// Negative for prograde orbits (the node regresses), positive for retrograde.
inline double j2_nodal_rate(double semi_major_axis_m, double eccentricity, double inclination_rad,
                            double j2 = constants::kEarthJ2) noexcept {
    const double a = semi_major_axis_m;
    const double p = a * (1.0 - eccentricity * eccentricity);
    const double n = std::sqrt(constants::kEarthGM / (a * a * a));
    const double re_over_p = constants::kEarthEquatorialRadius / p;
    return -1.5 * j2 * re_over_p * re_over_p * n * std::cos(inclination_rad);
}

}  // namespace abep
