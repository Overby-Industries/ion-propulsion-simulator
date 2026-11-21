#pragma once

#include "abep/constants.hpp"
#include "abep/orbit.hpp"
#include "abep/vec3.hpp"

namespace abep {

// Velocity of the satellite relative to the air. The atmosphere is taken to
// rotate rigidly with the Earth about the z axis, so the air at position r moves
// at omega x r. Pass corotating = false for a non-rotating atmosphere.
inline Vec3 velocity_relative_to_air(const State& s, bool corotating = true) noexcept {
    if (!corotating) return s.v;
    const double w = constants::kEarthRotationRate;
    // omega x r = (-w*y, w*x, 0)
    return {s.v.x + w * s.r.y, s.v.y - w * s.r.x, s.v.z};
}

// Drag acceleration, m/s^2:  a = -1/2 * rho * (Cd * A / m) * |v_rel| * v_rel.
// `density` in kg/m^3, `area_m2` is the frontal area, `mass_kg` the satellite mass.
inline Vec3 drag_acceleration(const State& s, double density, double drag_coefficient, double area_m2,
                              double mass_kg, bool corotating = true) noexcept {
    const Vec3 v_rel = velocity_relative_to_air(s, corotating);
    const double scale = -0.5 * density * drag_coefficient * area_m2 / mass_kg * norm(v_rel);
    return scale * v_rel;
}

}  // namespace abep
