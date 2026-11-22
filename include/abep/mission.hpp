#pragma once

// A mission run: the orbit, drag, thrust, power and thermal models of this library, driven by the flight state machine
// (flight.hpp), one fixed step at a time. The equations and what is left out are in docs/ABEP_MODEL.md and
// docs/FLIGHT_MODES.md. Deterministic: a fixed step, no random numbers, and the same inputs give the same outputs.
//
// Each step does, in this order:
//   1. Look: altitude above the ellipsoid, density there, speed relative to the air, sunlight or shadow.
//   2. Decide: build the flags and let the state machine choose the mode for the step.
//   3. Power: the array's output (zero in shadow or during an outage), the bus load the mode implies, and, in THRUST,
//      the thruster power the supply and the battery can deliver, which the thrust model turns into thrust and an actual
//      power draw (less than offered if the intake is the limit).
//   4. Move: one RK4 step of gravity (with or without J2), drag and thrust, the thrust held constant over the step and
//      pointing along the inertial velocity.
//   5. Account: the battery, the thruster's temperature, the time in each mode, the energy balance of the orbit.
//
// The mode is chosen from what the satellite sees at the start of the step, so a mode change takes effect one step late
// at most. The step should be small against the times that matter (the thermal time constant, the time it takes to
// cross the shadow edge).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <vector>

#include "abep/atmosphere.hpp"
#include "abep/drag.hpp"
#include "abep/eclipse.hpp"
#include "abep/flight.hpp"
#include "abep/orbit.hpp"
#include "abep/power.hpp"
#include "abep/thermal.hpp"
#include "abep/thrust.hpp"
#include "abep/vec3.hpp"

namespace abep {

// The satellite. Every field has to be set; a zero is refused where zero makes no sense.
struct Satellite {
    double mass_kg = 0.0;
    double frontal_area_m2 = 0.0;
    double drag_coefficient = 0.0;
    IntakeThruster thruster{};
    double thruster_power_w = 0.0;        // electrical power offered to the thruster in THRUST
    double array_power_w = 0.0;           // array output in sunlight
    double bus_load_w = 0.0;              // housekeeping load in every mode but SAFE and FAULT
    double safe_load_w = 0.0;             // housekeeping load in SAFE and FAULT
    double battery_capacity_wh = 0.0;
    double battery_initial_wh = 0.0;
    std::optional<ThrusterThermal> thermal;  // none: the temperature stays at its initial value
    double initial_temperature_k = 0.0;
};

// A span of time, [start, end), in seconds from the start of the run.
struct TimeWindow {
    double start_s = 0.0;
    double end_s = 0.0;
};

struct Scenario {
    State initial{};                          // inertial position and velocity
    Vec3 sun_unit{1.0, 0.0, 0.0};             // a fixed direction to the Sun, a unit vector
    double duration_s = 0.0;
    double step_s = 10.0;
    bool include_j2 = true;
    bool corotating_atmosphere = true;
    bool thruster_enabled = true;             // false: the thruster is never used (a drag-only run)
    std::vector<TimeWindow> array_outages;    // the array produces nothing
    std::vector<TimeWindow> fault_windows;    // an external fault is asserted
    std::size_t sample_every = 1;             // keep one sample in this many steps
};

enum class EndReason {
    Completed,           // the run lasted its full duration
    AltitudeOutOfRange   // the density table does not reach the satellite's altitude: it decayed, or never was in it
};

// The state at the start of a step, and what was done during it.
struct Sample {
    double t_s = 0.0;
    Mode mode = Mode::Safe;
    double altitude_m = 0.0;         // above the ellipsoid
    double density_kg_m3 = 0.0;
    double relative_speed_m_s = 0.0; // speed relative to the air, which co-rotates with Earth unless told otherwise
    double drag_n = 0.0;             // 1/2 rho v_rel^2 Cd A
    double thrust_n = 0.0;
    double net_drag_n = 0.0;         // drag minus thrust: positive slows the satellite
    double thruster_power_w = 0.0;   // electrical power drawn by the thruster
    double generation_w = 0.0;
    double load_w = 0.0;
    double battery_wh = 0.0;
    double temperature_k = 0.0;
    bool sunlit = true;              // geometry only: the array can still be out
};

// The books for one orbit, an orbit being a span of the two-body period of the initial orbit.
struct OrbitBalance {
    double duration_s = 0.0;          // time covered; the last orbit may be partial
    double thrust_s = 0.0;            // time in THRUST
    double eclipse_s = 0.0;
    double generated_wh = 0.0;
    double load_wh = 0.0;
    double thruster_wh = 0.0;
    double battery_change_wh = 0.0;
    // Energy the battery could not take (positive: it was full) or could not give (negative: it was empty).
    double spilled_wh = 0.0;

    double duty() const noexcept { return duration_s > 0.0 ? thrust_s / duration_s : 0.0; }
};

struct MissionResult {
    std::vector<Sample> samples;
    std::vector<OrbitBalance> orbits;
    std::array<double, kModeCount> seconds_in_mode{};  // indexed by static_cast<std::size_t>(Mode)
    double elapsed_s = 0.0;
    EndReason end_reason = EndReason::Completed;
    State final_state{};
    double final_battery_wh = 0.0;
    double final_temperature_k = 0.0;

    double seconds_in(Mode m) const noexcept { return seconds_in_mode[static_cast<std::size_t>(m)]; }
    // The share of the run spent in THRUST.
    double thrust_duty() const noexcept { return elapsed_s > 0.0 ? seconds_in(Mode::Thrust) / elapsed_s : 0.0; }
};

// Throws std::invalid_argument for a satellite that is not a physical one.
inline void validate(const Satellite& s) {
    const auto positive = [](double x) { return std::isfinite(x) && x > 0.0; };
    const auto non_negative = [](double x) { return std::isfinite(x) && x >= 0.0; };
    if (!positive(s.mass_kg)) throw std::invalid_argument("mass must be positive and finite");
    if (!positive(s.frontal_area_m2)) throw std::invalid_argument("frontal area must be positive and finite");
    if (!positive(s.drag_coefficient)) throw std::invalid_argument("drag coefficient must be positive and finite");
    validate(s.thruster);
    if (!non_negative(s.thruster_power_w)) throw std::invalid_argument("thruster power must be zero or more, and finite");
    if (!non_negative(s.array_power_w)) throw std::invalid_argument("array power must be zero or more, and finite");
    if (!non_negative(s.bus_load_w)) throw std::invalid_argument("bus load must be zero or more, and finite");
    if (!non_negative(s.safe_load_w)) throw std::invalid_argument("safe-mode load must be zero or more, and finite");
    if (!(positive(s.battery_capacity_wh) && s.battery_initial_wh >= 0.0 && s.battery_initial_wh <= s.battery_capacity_wh)) {
        throw std::invalid_argument("the battery needs a positive capacity and an initial energy from 0 to the capacity");
    }
    if (s.thermal) validate(*s.thermal);
    if (!positive(s.initial_temperature_k)) throw std::invalid_argument("initial temperature must be positive (kelvin) and finite");
}

// Throws std::invalid_argument for a scenario that cannot be run.
inline void validate(const Scenario& sc) {
    if (!(std::isfinite(sc.duration_s) && sc.duration_s >= 0.0)) throw std::invalid_argument("duration must be zero or more, and finite");
    if (!(std::isfinite(sc.step_s) && sc.step_s > 0.0)) throw std::invalid_argument("step must be positive and finite");
    if (sc.sample_every < 1) throw std::invalid_argument("sample_every must be at least 1");
    if (!(std::fabs(norm(sc.sun_unit) - 1.0) <= 1.0e-9)) throw std::invalid_argument("the Sun direction must be a unit vector");
    if (!(std::isfinite(norm(sc.initial.r)) && norm(sc.initial.r) > 0.0 && std::isfinite(norm(sc.initial.v)))) {
        throw std::invalid_argument("the initial state must be finite, with a position away from Earth's centre");
    }
    if (!(specific_energy(sc.initial) < 0.0)) throw std::invalid_argument("the initial state must be on a bound orbit");
    for (const auto* windows : {&sc.array_outages, &sc.fault_windows}) {
        for (const TimeWindow& w : *windows) {
            if (!(std::isfinite(w.start_s) && std::isfinite(w.end_s) && w.start_s <= w.end_s)) {
                throw std::invalid_argument("a time window needs finite start <= end");
            }
        }
    }
}

namespace detail {

inline bool inside_any(const std::vector<TimeWindow>& windows, double t_s) noexcept {
    for (const TimeWindow& w : windows) {
        if (t_s >= w.start_s && t_s < w.end_s) return true;
    }
    return false;
}

// A shortfall smaller than this fraction of the battery's capacity is rounding, not a brownout. One unit roundoff of
// the stored energy is about 1e-16 of the capacity.
inline constexpr double kShortfallFraction = 1.0e-9;

}  // namespace detail

// Run the mission. `atmosphere` gives the density (see standard_atmosphere_1976.hpp); a satellite that leaves the range
// it covers ends the run with EndReason::AltitudeOutOfRange instead of getting an invented density. Throws
// std::invalid_argument for invalid inputs.
inline MissionResult run_mission(const Satellite& sat, const Scenario& sc, const ControllerConfig& config,
                                 const DensityTable& atmosphere) {
    validate(sat);
    validate(sc);
    const double period_s = orbital_period_two_body(semi_major_axis(sc.initial));

    FlightComputer computer(config);
    Battery battery(sat.battery_capacity_wh, sat.battery_initial_wh);
    // Of the electrical power a thruster draws, the jet carries 1/2 tau c_e; the rest is taken to heat the thruster.
    const double waste_fraction = 1.0 - implied_thruster_efficiency(sat.thruster);
    double temperature_k = sat.initial_temperature_k;
    State state = sc.initial;
    bool brownout = false;

    const long long whole_steps = static_cast<long long>(sc.duration_s / sc.step_s);
    const double rest_s = sc.duration_s - static_cast<double>(whole_steps) * sc.step_s;
    const long long total_steps = whole_steps + (rest_s > 0.0 ? 1 : 0);

    MissionResult out;
    out.samples.reserve(static_cast<std::size_t>(total_steps) / sc.sample_every + 1);

    for (long long i = 0; i < total_steps; ++i) {
        const double t_s = static_cast<double>(i) * sc.step_s;
        const double h_s = i < whole_steps ? sc.step_s : rest_s;

        // 1. Look.
        const double altitude_m = altitude_above_ellipsoid(state.r);
        double density = 0.0;
        try {
            density = atmosphere.density(altitude_m);
        } catch (const std::out_of_range&) {
            out.end_reason = EndReason::AltitudeOutOfRange;
            break;
        }
        const double relative_speed = norm(velocity_relative_to_air(state, sc.corotating_atmosphere));
        const bool in_sun = !in_earth_shadow(state.r, sc.sun_unit);
        const bool array_on = in_sun && !detail::inside_any(sc.array_outages, t_s);

        // 2. Decide.
        Flags flags;
        flags.battery_fraction = battery.energy_wh() / battery.capacity_wh();
        flags.sunlit = array_on;
        flags.density_kg_m3 = density;
        flags.temperature_k = temperature_k;
        flags.thruster_available = sc.thruster_enabled;
        flags.fault = brownout || detail::inside_any(sc.fault_windows, t_s);
        const Mode mode = computer.update(flags, h_s);

        // 3. Power. In THRUST the thruster is offered what it is rated for, or what the supply and the battery can give
        // over this step if that is less, so that the thruster alone never empties the battery.
        const double generation_w = array_on ? sat.array_power_w : 0.0;
        const double load_w = (mode == Mode::Safe || mode == Mode::Fault) ? sat.safe_load_w : sat.bus_load_w;
        ThrustResult thrust{};
        if (mode == Mode::Thrust) {
            const double deliverable_w = generation_w - load_w + battery.energy_wh() * 3600.0 / h_s;
            const double offered_w = std::min(sat.thruster_power_w, std::max(0.0, deliverable_w));
            thrust = abep_thrust(sat.thruster, offered_w, density, relative_speed);
        }
        const double net_w = generation_w - load_w - thrust.power_w;

        // 4. Move.
        const double thrust_n = thrust.thrust_n;
        const auto acceleration = [&](double, const State& st) {
            Vec3 a = gravity_acceleration(st.r, sc.include_j2);
            const double air = atmosphere.density(altitude_above_ellipsoid(st.r));
            a = a + drag_acceleration(st, air, sat.drag_coefficient, sat.frontal_area_m2, sat.mass_kg, sc.corotating_atmosphere);
            if (thrust_n > 0.0) a = a + (thrust_n / (sat.mass_kg * norm(st.v))) * st.v;
            return a;
        };
        State next;
        try {
            next = rk4_step(state, t_s, h_s, acceleration);
        } catch (const std::out_of_range&) {
            out.end_reason = EndReason::AltitudeOutOfRange;
            break;
        }

        // 5. Account.
        const double battery_before_wh = battery.energy_wh();
        const double wanted_wh = net_w * h_s / 3600.0;
        const double actual_wh = battery.apply(net_w, h_s);
        const double spilled_wh = wanted_wh - actual_wh;
        brownout = spilled_wh < -detail::kShortfallFraction * battery.capacity_wh();
        if (sat.thermal) temperature_k = thermal_step(*sat.thermal, temperature_k, thrust.power_w * waste_fraction, h_s);

        out.seconds_in_mode[static_cast<std::size_t>(mode)] += h_s;
        out.elapsed_s += h_s;
        const std::size_t orbit_index = static_cast<std::size_t>(t_s / period_s);
        if (orbit_index >= out.orbits.size()) out.orbits.resize(orbit_index + 1);
        OrbitBalance& balance = out.orbits[orbit_index];
        balance.duration_s += h_s;
        if (mode == Mode::Thrust) balance.thrust_s += h_s;
        if (!in_sun) balance.eclipse_s += h_s;
        balance.generated_wh += generation_w * h_s / 3600.0;
        balance.load_wh += load_w * h_s / 3600.0;
        balance.thruster_wh += thrust.power_w * h_s / 3600.0;
        balance.battery_change_wh += actual_wh;
        balance.spilled_wh += spilled_wh;

        if (static_cast<std::size_t>(i) % sc.sample_every == 0) {
            Sample sample;
            sample.t_s = t_s;
            sample.mode = mode;
            sample.altitude_m = altitude_m;
            sample.density_kg_m3 = density;
            sample.relative_speed_m_s = relative_speed;
            sample.drag_n = drag_force(density, relative_speed, sat.drag_coefficient, sat.frontal_area_m2);
            sample.thrust_n = thrust_n;
            sample.net_drag_n = sample.drag_n - thrust_n;
            sample.thruster_power_w = thrust.power_w;
            sample.generation_w = generation_w;
            sample.load_w = load_w;
            sample.battery_wh = battery_before_wh;  // like the rest, as it was at the start of the step
            sample.temperature_k = flags.temperature_k;
            sample.sunlit = in_sun;
            out.samples.push_back(sample);
        }
        state = next;
    }

    out.final_state = state;
    out.final_battery_wh = battery.energy_wh();
    out.final_temperature_k = temperature_k;
    return out;
}

}  // namespace abep
