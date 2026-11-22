#pragma once

// A run described by named numbers, in the units they are usually quoted in: altitude in km, thrust density in mN/kW, and so on.
// It is how the command-line program (examples/abep_run.cpp) describes a run, and it builds the Satellite, the Scenario and the
// ControllerConfig that run_mission() takes. Every setting is a double with a default, so a run needs only what differs from it;
// an on/off setting is 0 or 1. The defaults are illustrative, not data about any real satellite or thruster, and the thermal
// node, which has no sourced numbers, is off unless a heat capacity is given. The settings are listed in docs/OUTPUT.md.

#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "abep/atmosphere_choice.hpp"
#include "abep/eclipse.hpp"
#include "abep/flight.hpp"
#include "abep/mission.hpp"
#include "abep/orbit.hpp"
#include "abep/thermal.hpp"
#include "abep/thrust.hpp"
#include "abep/units.hpp"

namespace abep {

struct RunSettings {
    // The orbit: circular, with the Sun fixed along +x, so the beta angle follows from the inclination and the node.
    double altitude_km = 250.0;
    double inclination_deg = 51.6;
    double raan_deg = 30.0;
    // The satellite.
    double mass_kg = 100.0;
    double frontal_area_m2 = 1.0;
    double drag_coefficient = 2.2;
    // The thruster.
    double thrust_density_mn_per_kw = 13.7;
    double exhaust_velocity_km_s = 20.0;
    double intake_area_m2 = 2.0;
    double capture_efficiency = 0.46;
    double thruster_power_w = 1000.0;
    // Power.
    double array_power_w = 1500.0;
    double bus_load_w = 300.0;
    double safe_load_w = 100.0;
    double battery_capacity_wh = 800.0;
    double battery_initial_wh = 600.0;
    // The thruster's temperature. A heat capacity of 0 leaves the node out; a limit of 0 leaves that limit out.
    double thermal_heat_capacity_j_per_k = 0.0;
    double thermal_conductance_w_per_k = 5.0;
    double thermal_sink_k = 300.0;
    double temperature_resume_k = 0.0;
    double temperature_limit_k = 0.0;
    double temperature_fault_k = 0.0;
    // The state machine's thresholds (the same defaults as ControllerConfig).
    double soc_safe_enter = 0.10;
    double soc_safe_exit = 0.30;
    double soc_thrust_stop = 0.20;
    double soc_thrust_start = 0.50;
    double thrust_in_eclipse = 0.0;
    double fault_hold_s = 600.0;
    // The run.
    double duration_days = 1.0;
    double step_s = 10.0;
    double sample_every = 6.0;
    double include_j2 = 1.0;
    double corotating_atmosphere = 1.0;
    double atmosphere = 0.0;  // 0: the 1976 US Standard Atmosphere; 1 to 4: ECSS-E-ST-10-04C low, moderate, high (long term), high (short term)
    double thruster_enabled = 1.0;
    // The summaries.
    double decay_altitude_km = 0.0;  // 0: the lower edge of the density table
    double decay_max_years = 10.0;
};

struct SettingInfo {
    const char* key;
    double RunSettings::*field;
    const char* meaning;
};

// Every setting, in the order they are listed in the documentation.
inline const std::vector<SettingInfo>& setting_table() {
    static const std::vector<SettingInfo> table = {
        {"altitude_km", &RunSettings::altitude_km, "circular orbit, altitude above the equatorial radius, km"},
        {"inclination_deg", &RunSettings::inclination_deg, "orbit inclination, deg"},
        {"raan_deg", &RunSettings::raan_deg, "right ascension of the ascending node, deg (the Sun is along +x)"},
        {"mass_kg", &RunSettings::mass_kg, "satellite mass, kg"},
        {"frontal_area_m2", &RunSettings::frontal_area_m2, "frontal area, m^2"},
        {"drag_coefficient", &RunSettings::drag_coefficient, "drag coefficient"},
        {"thrust_density_mn_per_kw", &RunSettings::thrust_density_mn_per_kw, "thrust per electrical power, mN/kW"},
        {"exhaust_velocity_km_s", &RunSettings::exhaust_velocity_km_s, "exhaust velocity, km/s"},
        {"intake_area_m2", &RunSettings::intake_area_m2, "intake area, m^2"},
        {"capture_efficiency", &RunSettings::capture_efficiency, "share of the air swept up that reaches the thruster, 0 to 1"},
        {"thruster_power_w", &RunSettings::thruster_power_w, "electrical power offered to the thruster in THRUST, W"},
        {"array_power_w", &RunSettings::array_power_w, "solar array output in sunlight, W"},
        {"bus_load_w", &RunSettings::bus_load_w, "housekeeping load, W"},
        {"safe_load_w", &RunSettings::safe_load_w, "housekeeping load in SAFE and FAULT, W"},
        {"battery_capacity_wh", &RunSettings::battery_capacity_wh, "battery capacity, Wh"},
        {"battery_initial_wh", &RunSettings::battery_initial_wh, "battery energy at the start, Wh"},
        {"thermal_heat_capacity_j_per_k", &RunSettings::thermal_heat_capacity_j_per_k, "thruster heat capacity, J/K (0: no thermal node)"},
        {"thermal_conductance_w_per_k", &RunSettings::thermal_conductance_w_per_k, "heat lost to the sink per kelvin, W/K"},
        {"thermal_sink_k", &RunSettings::thermal_sink_k, "sink temperature, K (also the temperature at the start)"},
        {"temperature_resume_k", &RunSettings::temperature_resume_k, "thrusting may resume at or below this, K (0: no limit)"},
        {"temperature_limit_k", &RunSettings::temperature_limit_k, "thrusting stops at or above this, K (0: no limit)"},
        {"temperature_fault_k", &RunSettings::temperature_fault_k, "a fault at or above this, K (0: no limit)"},
        {"soc_safe_enter", &RunSettings::soc_safe_enter, "battery fraction below which SAFE is entered"},
        {"soc_safe_exit", &RunSettings::soc_safe_exit, "battery fraction at which SAFE is left"},
        {"soc_thrust_stop", &RunSettings::soc_thrust_stop, "battery fraction below which thrusting stops"},
        {"soc_thrust_start", &RunSettings::soc_thrust_start, "battery fraction at which thrusting may start or restart"},
        {"thrust_in_eclipse", &RunSettings::thrust_in_eclipse, "1: thrust in eclipse too, 0: in sunlight only"},
        {"fault_hold_s", &RunSettings::fault_hold_s, "time FAULT holds after the fault clears, s"},
        {"duration_days", &RunSettings::duration_days, "length of the run, days"},
        {"step_s", &RunSettings::step_s, "time step, s"},
        {"sample_every", &RunSettings::sample_every, "keep one sample in this many steps (a whole number)"},
        {"include_j2", &RunSettings::include_j2, "1: gravity with J2, 0: point mass"},
        {"corotating_atmosphere", &RunSettings::corotating_atmosphere, "1: the air turns with the Earth, 0: it does not"},
        {"atmosphere", &RunSettings::atmosphere, "0: the 1976 US Standard Atmosphere; 1, 2, 3, 4: ECSS low, moderate, high (long term), high (short term) solar activity"},
        {"thruster_enabled", &RunSettings::thruster_enabled, "0: a drag-only run"},
        {"decay_altitude_km", &RunSettings::decay_altitude_km, "decay altitude for the decay time, km (0: the lower edge of the table)"},
        {"decay_max_years", &RunSettings::decay_max_years, "longest time to simulate for the decay time, years"},
    };
    return table;
}

// Sets one setting by name. Throws std::invalid_argument for a name that is not a setting and for a value that is not finite;
// whether the value makes sense is checked by make_mission().
inline void set_setting(RunSettings& settings, const std::string& key, double value) {
    if (!std::isfinite(value)) throw std::invalid_argument("setting '" + key + "' needs a finite number");
    for (const SettingInfo& s : setting_table()) {
        if (key == s.key) {
            settings.*(s.field) = value;
            return;
        }
    }
    throw std::invalid_argument("unknown setting '" + key + "'");
}

struct Mission {
    Satellite satellite;
    Scenario scenario;
    ControllerConfig controller;
};

// Builds what run_mission() takes from the settings. Throws std::invalid_argument, naming the setting where it can, for a value
// that makes no sense.
inline Mission make_mission(const RunSettings& s) {
    const auto flag = [](double v, const char* name) {
        if (v == 0.0) return false;
        if (v == 1.0) return true;
        throw std::invalid_argument(std::string("setting '") + name + "' must be 0 or 1");
    };
    if (!(s.sample_every >= 1.0 && s.sample_every <= 1.0e9 && s.sample_every == std::floor(s.sample_every))) {
        throw std::invalid_argument("setting 'sample_every' must be a whole number from 1 to 1e9");
    }
    if (!(std::isfinite(s.duration_days) && s.duration_days >= 0.0)) {
        throw std::invalid_argument("setting 'duration_days' must be zero or more, and finite");
    }

    Mission m;
    Satellite& sat = m.satellite;
    sat.mass_kg = s.mass_kg;
    sat.frontal_area_m2 = s.frontal_area_m2;
    sat.drag_coefficient = s.drag_coefficient;
    sat.thruster = {units::mn_per_kw_to_n_per_w(s.thrust_density_mn_per_kw), 1.0e3 * s.exhaust_velocity_km_s, s.intake_area_m2,
                    s.capture_efficiency};
    sat.thruster_power_w = s.thruster_power_w;
    sat.array_power_w = s.array_power_w;
    sat.bus_load_w = s.bus_load_w;
    sat.safe_load_w = s.safe_load_w;
    sat.battery_capacity_wh = s.battery_capacity_wh;
    sat.battery_initial_wh = s.battery_initial_wh;
    sat.initial_temperature_k = s.thermal_sink_k;
    if (s.thermal_heat_capacity_j_per_k > 0.0) {
        sat.thermal = ThrusterThermal{s.thermal_heat_capacity_j_per_k, s.thermal_conductance_w_per_k, s.thermal_sink_k};
    }

    Scenario& sc = m.scenario;
    sc.initial = circular_orbit_state(units::km_to_m(s.altitude_km), units::deg_to_rad(s.inclination_deg),
                                      units::deg_to_rad(s.raan_deg), 0.0);
    sc.sun_unit = sun_direction_from_ra_dec(0.0, 0.0);
    sc.duration_s = 86400.0 * s.duration_days;
    sc.step_s = s.step_s;
    sc.include_j2 = flag(s.include_j2, "include_j2");
    sc.corotating_atmosphere = flag(s.corotating_atmosphere, "corotating_atmosphere");
    (void)atmosphere_choice(s.atmosphere);  // refuses a value that names no atmosphere; simulate() picks the table
    sc.thruster_enabled = flag(s.thruster_enabled, "thruster_enabled");
    sc.sample_every = static_cast<std::size_t>(s.sample_every);

    ControllerConfig& c = m.controller;
    c.soc_safe_enter = s.soc_safe_enter;
    c.soc_safe_exit = s.soc_safe_exit;
    c.soc_thrust_stop = s.soc_thrust_stop;
    c.soc_thrust_start = s.soc_thrust_start;
    c.thrust_in_eclipse = flag(s.thrust_in_eclipse, "thrust_in_eclipse");
    c.fault_hold_s = s.fault_hold_s;
    if (s.temperature_resume_k > 0.0) c.temperature_resume_k = s.temperature_resume_k;
    if (s.temperature_limit_k > 0.0) c.temperature_limit_k = s.temperature_limit_k;
    if (s.temperature_fault_k > 0.0) c.temperature_fault_k = s.temperature_fault_k;

    validate(sat);
    validate(sc);
    validate(c);
    return m;
}

}  // namespace abep
