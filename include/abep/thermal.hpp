#pragma once

// A lumped thermal node for the thruster: one temperature, heated by the part of the electrical power that does not
// leave as jet power, cooled by a conductance to a fixed sink. It exists so that the flight state machine has a
// thermal flag to react to. Every number it takes is an input with no source behind it, and nothing here says that
// any real thruster behaves this way (docs/FLIGHT_MODES.md). The model is linear, so a step is exact for a constant
// heat input:
//
//     C dT/dt = Q - G (T - T_sink)   =>   T(t) = T_inf + (T0 - T_inf) exp(-G t / C),   T_inf = T_sink + Q / G.

#include <cmath>
#include <stdexcept>

namespace abep {

struct ThrusterThermal {
    double heat_capacity_j_per_k;  // C
    double conductance_w_per_k;    // G: heat lost to the sink per kelvin of difference; zero means no cooling
    double sink_temperature_k;     // T_sink
};

// Throws std::invalid_argument unless C > 0, G >= 0 and T_sink > 0, all finite.
inline void validate(const ThrusterThermal& p) {
    if (!(std::isfinite(p.heat_capacity_j_per_k) && p.heat_capacity_j_per_k > 0.0)) {
        throw std::invalid_argument("heat capacity must be positive and finite");
    }
    if (!(std::isfinite(p.conductance_w_per_k) && p.conductance_w_per_k >= 0.0)) {
        throw std::invalid_argument("conductance must be zero or more, and finite");
    }
    if (!(std::isfinite(p.sink_temperature_k) && p.sink_temperature_k > 0.0)) {
        throw std::invalid_argument("sink temperature must be positive (kelvin) and finite");
    }
}

// The temperature the node settles at under a constant heat input, K: T_sink + Q / G. Throws std::invalid_argument if
// there is no cooling (the temperature then has no limit) or for invalid inputs.
inline double steady_state_temperature(const ThrusterThermal& p, double waste_heat_w) {
    validate(p);
    if (!(p.conductance_w_per_k > 0.0)) throw std::invalid_argument("a node with no cooling has no steady state");
    if (!(std::isfinite(waste_heat_w) && waste_heat_w >= 0.0)) throw std::invalid_argument("waste heat must be zero or more");
    return p.sink_temperature_k + waste_heat_w / p.conductance_w_per_k;
}

// The temperature after `dt_s` seconds with `waste_heat_w` of heat going in throughout, K. Exact for this linear model,
// so any split of a time span into steps gives the same answer. Throws std::invalid_argument for invalid inputs.
inline double thermal_step(const ThrusterThermal& p, double temperature_k, double waste_heat_w, double dt_s) {
    validate(p);
    if (!(std::isfinite(temperature_k) && temperature_k > 0.0)) throw std::invalid_argument("temperature must be positive and finite");
    if (!(std::isfinite(waste_heat_w) && waste_heat_w >= 0.0)) throw std::invalid_argument("waste heat must be zero or more");
    if (!(std::isfinite(dt_s) && dt_s >= 0.0)) throw std::invalid_argument("time step must be zero or more");
    if (p.conductance_w_per_k == 0.0) return temperature_k + waste_heat_w * dt_s / p.heat_capacity_j_per_k;
    const double t_inf = p.sink_temperature_k + waste_heat_w / p.conductance_w_per_k;
    return t_inf + (temperature_k - t_inf) * std::exp(-p.conductance_w_per_k * dt_s / p.heat_capacity_j_per_k);
}

}  // namespace abep
