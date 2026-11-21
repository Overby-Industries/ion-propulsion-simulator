#pragma once

// Sunlight and shadow. The Sun is a fixed unit vector in the inertial frame: there is no ephemeris, so the Sun does
// not move over the run (it really moves about 1 degree a day, which matters for runs of weeks). Earth's shadow is a
// cylinder of Earth's equatorial radius along the anti-Sun axis: no penumbra, no atmosphere, no oblateness. Both
// simplifications are in docs/SPEC.md.

#include <cmath>
#include <stdexcept>

#include "abep/constants.hpp"
#include "abep/orbit.hpp"
#include "abep/units.hpp"
#include "abep/vec3.hpp"

namespace abep {

// Unit vector towards the Sun from its right ascension and declination in the inertial frame.
inline Vec3 sun_direction_from_ra_dec(double ra_rad, double dec_rad) noexcept {
    return {std::cos(dec_rad) * std::cos(ra_rad), std::cos(dec_rad) * std::sin(ra_rad), std::sin(dec_rad)};
}

// True if the point r is in the cylindrical shadow: on the night side of the plane through Earth's centre
// perpendicular to the Sun direction, and strictly inside `shadow_radius_m` of the axis along it. `sun_unit` must be a
// unit vector. A point exactly on the terminator plane, or exactly on the cylinder, is sunlit.
inline bool in_earth_shadow(const Vec3& r, const Vec3& sun_unit,
                            double shadow_radius_m = constants::kEarthEquatorialRadius) noexcept {
    const double along = dot(r, sun_unit);
    if (!(along < 0.0)) return false;
    const Vec3 across = r - along * sun_unit;
    return dot(across, across) < shadow_radius_m * shadow_radius_m;
}

// Beta angle: the angle between the Sun direction and the orbit plane, in radians, positive when the Sun is on the
// side the angular momentum points to.
inline double beta_angle(const State& s, const Vec3& sun_unit) noexcept {
    const Vec3 h = cross(s.r, s.v);
    const double sine = dot(h, sun_unit) / norm(h);
    return std::asin(std::fmax(-1.0, std::fmin(1.0, sine)));
}

// The fraction of a circular orbit spent in the cylindrical shadow, in closed form:
//     f = acos( sqrt(1 - (R / r)^2) / cos(beta) ) / pi,   for |beta| < asin(R / r), and 0 otherwise.
// It follows from requiring the point at angle theta from the Sun's projection to satisfy r_hat . s_hat < 0 and
// |r|^2 (1 - (r_hat . s_hat)^2) < R^2 with r_hat . s_hat = cos(theta) cos(beta) (derived in docs/ABEP_MODEL.md;
// checked against sampling and against a propagated orbit in tests/test_eclipse.cpp). Throws std::invalid_argument
// if the orbit radius is not larger than the shadow radius.
inline double circular_orbit_eclipse_fraction(double orbit_radius_m, double beta_rad,
                                              double shadow_radius_m = constants::kEarthEquatorialRadius) {
    if (!(orbit_radius_m > shadow_radius_m)) throw std::invalid_argument("orbit radius must exceed the shadow radius");
    const double ratio = shadow_radius_m / orbit_radius_m;
    const double cos_beta = std::cos(beta_rad);
    if (!(cos_beta > 0.0)) return 0.0;
    const double x = std::sqrt(1.0 - ratio * ratio) / cos_beta;
    if (!(x < 1.0)) return 0.0;
    return std::acos(x) / units::kPi;
}

}  // namespace abep
