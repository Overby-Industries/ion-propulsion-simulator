#pragma once

// The air-breathing electric propulsion (ABEP) thrust model: thrust from electrical power and a thrust density,
// limited by the propellant the intake can collect. The equations, where each parameter comes from, and what is
// not modelled are in docs/ABEP_MODEL.md. In short:
//
//   collected mass flow   mdot_c = eta_c * rho * v_rel * A_in                 (Romano et al. 2018, Eq. 9)
//   power-limited thrust  F_p    = tau * P
//   propellant-limited    F_m    = mdot_c * c_e
//   thrust                F      = min(F_p, F_m)
//
// This is a parameterised model, not a thruster model: tau, c_e and eta_c are inputs, and nothing here says that
// any real thruster or intake achieves them.

#include <cmath>
#include <stdexcept>

namespace abep {

struct IntakeThruster {
    double thrust_density_n_per_w;  // tau: gross thrust per watt of electrical power when propellant is not limiting
    double exhaust_velocity_m_s;    // c_e
    double intake_area_m2;          // A_in: the area that collects air
    double capture_efficiency;      // eta_c, 0 to 1: the share of the air swept up that reaches the thruster
};

// The efficiency at which tau and c_e are consistent: jet power 1/2 F c_e over electrical power P = F / tau gives
// 1/2 tau c_e. It cannot exceed 1.
constexpr double implied_thruster_efficiency(const IntakeThruster& t) noexcept {
    return 0.5 * t.thrust_density_n_per_w * t.exhaust_velocity_m_s;
}

// Throws std::invalid_argument for inputs that are not finite, out of range, or that imply a thruster efficiency
// above 100% (that is, a thrust density and an exhaust velocity that cannot both be true).
inline void validate(const IntakeThruster& t) {
    const auto positive = [](double x) { return std::isfinite(x) && x > 0.0; };
    if (!positive(t.thrust_density_n_per_w)) throw std::invalid_argument("thrust density must be positive and finite");
    if (!positive(t.exhaust_velocity_m_s)) throw std::invalid_argument("exhaust velocity must be positive and finite");
    if (!(std::isfinite(t.intake_area_m2) && t.intake_area_m2 >= 0.0)) {
        throw std::invalid_argument("intake area must be zero or more, and finite");
    }
    if (!(t.capture_efficiency >= 0.0 && t.capture_efficiency <= 1.0)) {
        throw std::invalid_argument("capture efficiency must be between 0 and 1");
    }
    if (implied_thruster_efficiency(t) > 1.0) {
        throw std::invalid_argument(
            "thrust density times exhaust velocity implies a thruster efficiency above 100%");
    }
}

// Air the intake delivers to the thruster, kg/s: eta_c * rho * v_rel * A_in.
inline double collectable_mass_flow(const IntakeThruster& t, double density_kg_m3, double relative_speed_m_s) {
    if (!(std::isfinite(density_kg_m3) && density_kg_m3 >= 0.0)) throw std::invalid_argument("density must be zero or more");
    if (!(std::isfinite(relative_speed_m_s) && relative_speed_m_s >= 0.0)) {
        throw std::invalid_argument("relative speed must be zero or more");
    }
    return t.capture_efficiency * density_kg_m3 * relative_speed_m_s * t.intake_area_m2;
}

enum class ThrustLimit {
    Power,      // electrical power sets the thrust
    Propellant  // the intake cannot supply enough air for that power
};

struct ThrustResult {
    double thrust_n;                      // gross thrust
    double power_w;                       // electrical power actually drawn, thrust / tau; at most the power offered
    double mass_flow_kg_s;                // air actually used, thrust / c_e
    double collectable_mass_flow_kg_s;    // air the intake could deliver
    ThrustLimit limit;                    // what set the thrust; a tie counts as Power
};

// Thrust for `available_power_w` of electrical power, in air of the given density, at the given speed relative to
// that air. Throws std::invalid_argument for invalid inputs.
inline ThrustResult abep_thrust(const IntakeThruster& t, double available_power_w, double density_kg_m3,
                                double relative_speed_m_s) {
    validate(t);
    if (!(std::isfinite(available_power_w) && available_power_w >= 0.0)) {
        throw std::invalid_argument("available power must be zero or more");
    }
    const double supply = collectable_mass_flow(t, density_kg_m3, relative_speed_m_s);
    const double power_limited = t.thrust_density_n_per_w * available_power_w;
    const double propellant_limited = supply * t.exhaust_velocity_m_s;
    ThrustResult out{};
    out.collectable_mass_flow_kg_s = supply;
    if (propellant_limited < power_limited) {
        out.thrust_n = propellant_limited;
        out.limit = ThrustLimit::Propellant;
    } else {
        out.thrust_n = power_limited;
        out.limit = ThrustLimit::Power;
    }
    out.power_w = out.thrust_n / t.thrust_density_n_per_w;
    out.mass_flow_kg_s = out.thrust_n / t.exhaust_velocity_m_s;
    return out;
}

}  // namespace abep
