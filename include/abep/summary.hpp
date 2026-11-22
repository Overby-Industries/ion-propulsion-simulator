#pragma once

// The summary outputs of docs/SPEC.md, built on run_mission() so that they use the same orbit, drag, atmosphere and thrust
// models as everything else:
//
//   time_to_decay           how long the satellite stays up with the thruster off;
//   power_to_hold_altitude  the thruster power that cancels the drag averaged over one orbit, and what that asks of the array.
//
// The definitions, and what they leave out, are in docs/OUTPUT.md. Like the rest of the library they are deterministic.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

#include "abep/atmosphere.hpp"
#include "abep/flight.hpp"
#include "abep/mission.hpp"
#include "abep/orbit.hpp"
#include "abep/thrust.hpp"

namespace abep {

inline constexpr double kSecondsPerYear = 365.25 * 86400.0;  // the Julian year

struct DecayResult {
    bool decayed = false;           // the altitude reached the decay altitude within the time allowed
    double time_s = 0.0;            // how long that took; if it did not happen, how long was simulated
    double final_altitude_m = 0.0;  // above the ellipsoid, at the end of the run
};

// The time for the satellite to fall from its initial orbit to `decay_altitude_m` (above the ellipsoid) with the thruster
// off, in the atmosphere given. The orbit is propagated, with the scenario's step, J2 and rotating air, so the result is
// as good as the propagation: it is found to within one step, late or early by less than that. The altitude is checked at the
// start of each step, which makes the time late; the run also stops, early, if a step's own stages reach below the density table.
// A decay altitude of 0 (the default), or one below the density table, means the lower edge of the table, where the run has to
// stop anyway because there is no density below it; there a satellite falls several km in a step, so `final_altitude_m` is the
// altitude at the start of the last step, and can be that much above the edge. If the satellite is still above the decay altitude
// after `max_duration_s`, `decayed` is false and `time_s` is `max_duration_s`.
inline DecayResult time_to_decay(const Satellite& sat, const Scenario& scenario, const DensityTable& atmosphere,
                                 double decay_altitude_m = 0.0, double max_duration_s = 10.0 * kSecondsPerYear) {
    if (!(std::isfinite(decay_altitude_m) && decay_altitude_m >= 0.0)) {
        throw std::invalid_argument("the decay altitude must be zero (the lower edge of the density table) or more, and finite");
    }
    if (!(std::isfinite(max_duration_s) && max_duration_s > 0.0)) {
        throw std::invalid_argument("the longest time to simulate must be positive and finite");
    }
    Scenario sc = scenario;
    sc.thruster_enabled = false;
    sc.duration_s = max_duration_s;
    sc.sample_every = std::numeric_limits<std::size_t>::max();  // one sample, at the start: only the end matters here
    sc.stop_below_altitude_m = std::max(decay_altitude_m, atmosphere.min_altitude_m());
    const MissionResult run = run_mission(sat, sc, ControllerConfig{}, atmosphere);
    DecayResult out;
    out.decayed = run.end_reason != EndReason::Completed;
    out.time_s = run.elapsed_s;
    out.final_altitude_m = altitude_above_ellipsoid(run.final_state.r);
    return out;
}

struct HoldResult {
    enum class Status {
        Holds,         // some thruster power cancels the orbit-averaged drag
        IntakeLimited  // the intake cannot collect enough air for that, at any power
    };
    Status status = Status::IntakeLimited;
    double power_w = std::numeric_limits<double>::infinity();         // the thruster's electrical power, thrusting all the time
    double mean_drag_n = 0.0;                                        // drag, averaged over one orbit
    double mean_intake_limit_n = 0.0;                                // the most thrust the intake can support, averaged over one orbit
    double sunlit_fraction = 1.0;                                    // of the orbit
    // The array's output in sunlight that carries `power_w` and the bus load round the whole orbit, eclipse included, with an
    // ideal battery that is big enough: (power_w + bus load) / sunlit_fraction.
    double array_power_w = std::numeric_limits<double>::infinity();
};

constexpr const char* hold_status_name(HoldResult::Status s) noexcept {
    return s == HoldResult::Status::Holds ? "Holds" : "IntakeLimited";
}

// The thruster power that holds the satellite's altitude: that makes the thrust, averaged over one orbit, equal the drag,
// averaged over the same orbit. The orbit is the scenario's initial one, propagated for one two-body period with the thruster
// off, so the density and the relative speed at each step are the mission run's own; the thrust at each step is min(tau * P,
// the intake's limit) as in the thrust model, with the same power P at every step. The weights are the lengths of the steps.
// Solved exactly (the thrust summed over the orbit is piecewise linear in P), not iterated.
//
// It leaves out the battery and the eclipse (the thruster is taken to run all the time: `array_power_w` says what that asks of
// the array), the thruster's rated power (compare `power_w` with it), and the small angle between the velocity relative to
// the air, which the drag follows, and the inertial velocity, which the thrust follows. Throws std::invalid_argument for
// inputs that cannot be run, or if the satellite leaves the density table within one orbit.
inline HoldResult power_to_hold_altitude(const Satellite& sat, const Scenario& scenario, const DensityTable& atmosphere) {
    validate(sat);
    validate(scenario);
    Scenario sc = scenario;
    sc.thruster_enabled = false;
    sc.duration_s = orbital_period_two_body(semi_major_axis(sc.initial));
    sc.sample_every = 1;
    sc.stop_below_altitude_m = 0.0;
    sc.array_outages.clear();
    sc.fault_windows.clear();
    const MissionResult run = run_mission(sat, sc, ControllerConfig{}, atmosphere);
    if (run.end_reason != EndReason::Completed || run.samples.empty()) {
        throw std::invalid_argument("the satellite leaves the density table within one orbit, so there is no orbit to hold");
    }

    struct Point {
        double cap_n;     // the most thrust the intake supports at this step
        double weight_s;  // the length of the step
    };
    std::vector<Point> points;
    points.reserve(run.samples.size());
    double total_s = 0.0;
    double drag_ns = 0.0;
    double cap_ns = 0.0;
    double sunlit_s = 0.0;
    for (const Sample& s : run.samples) {
        const double weight_s = std::min(sc.step_s, sc.duration_s - s.t_s);
        const double cap_n =
            collectable_mass_flow(sat.thruster, s.density_kg_m3, s.relative_speed_m_s) * sat.thruster.exhaust_velocity_m_s;
        points.push_back({cap_n, weight_s});
        total_s += weight_s;
        drag_ns += weight_s * s.drag_n;
        cap_ns += weight_s * cap_n;
        if (s.sunlit) sunlit_s += weight_s;
    }

    HoldResult out;
    out.mean_drag_n = drag_ns / total_s;
    out.mean_intake_limit_n = cap_ns / total_s;
    out.sunlit_fraction = sunlit_s / total_s;

    // Find X = tau * P with  sum w_i min(X, cap_i) = sum w_i drag_i. The left side rises with X. Take the caps in increasing
    // order: those below the answer are held at their cap, and the rest share what is left.
    std::sort(points.begin(), points.end(), [](const Point& a, const Point& b) { return a.cap_n < b.cap_n; });
    double capped_ns = 0.0;   // thrust times time delivered by the steps held at their cap
    double free_s = total_s;  // time of the steps that are not
    for (const Point& p : points) {
        const double x = (drag_ns - capped_ns) / free_s;
        if (x <= p.cap_n) {
            out.status = HoldResult::Status::Holds;
            out.power_w = x / sat.thruster.thrust_density_n_per_w;
            out.array_power_w =
                out.sunlit_fraction > 0.0 ? (out.power_w + sat.bus_load_w) / out.sunlit_fraction : out.array_power_w;
            return out;
        }
        capped_ns += p.weight_s * p.cap_n;
        free_s -= p.weight_s;
    }
    return out;  // every step at its cap and still short of the drag: IntakeLimited
}

}  // namespace abep
