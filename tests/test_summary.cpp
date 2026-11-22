#include "check.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "abep/atmosphere.hpp"
#include "abep/constants.hpp"
#include "abep/eclipse.hpp"
#include "abep/flight.hpp"
#include "abep/mission.hpp"
#include "abep/orbit.hpp"
#include "abep/standard_atmosphere_1976.hpp"
#include "abep/summary.hpp"
#include "abep/thrust.hpp"
#include "abep/units.hpp"

namespace {

namespace units = abep::units;

const abep::DensityTable& ussa() {
    static const abep::DensityTable table = abep::ussa1976::density_table();
    return table;
}

// A satellite of 100 kg, 1 m^2 and Cd 2.2 (CdA/m = 0.022 m^2/kg), with a thruster whose intake is large enough that the air does not
// limit the thrust unless a test sets it. Illustrative, not data about any real one.
abep::Satellite satellite() {
    abep::Satellite s;
    s.mass_kg = 100.0;
    s.frontal_area_m2 = 1.0;
    s.drag_coefficient = 2.2;
    s.thruster = {units::mn_per_kw_to_n_per_w(13.7), 20.0e3, 1000.0, 1.0};
    s.thruster_power_w = 1000.0;
    s.array_power_w = 2000.0;
    s.bus_load_w = 300.0;
    s.safe_load_w = 100.0;
    s.battery_capacity_wh = 1000.0;
    s.battery_initial_wh = 800.0;
    s.initial_temperature_k = 300.0;
    return s;
}

// A circular orbit with the Sun along +x, J2 left out and the air not turning, so that what happens to it is drag alone.
abep::Scenario scenario(double altitude_m, double inclination_deg, double duration_s) {
    abep::Scenario sc;
    sc.initial = abep::circular_orbit_state(altitude_m, units::deg_to_rad(inclination_deg), 0.0, 0.0);
    sc.sun_unit = abep::sun_direction_from_ra_dec(0.0, 0.0);
    sc.duration_s = duration_s;
    sc.step_s = 10.0;
    sc.include_j2 = false;
    sc.corotating_atmosphere = false;
    return sc;
}

// An exactly exponential atmosphere, rho0 at h0 with scale height H, between 100 km and 400 km. The table interpolates
// exponentially between its nodes, so it reproduces this exactly.
abep::DensityTable exponential_table(double rho0, double h0_m, double scale_height_m) {
    std::vector<abep::DensityTable::Node> nodes;
    for (int i = 0; i <= 30; ++i) {
        const double h = 100.0e3 + 10.0e3 * static_cast<double>(i);
        nodes.push_back({h, rho0 * std::exp(-(h - h0_m) / scale_height_m)});
    }
    return abep::DensityTable(nodes);
}

// The same density everywhere.
abep::DensityTable uniform_table(double rho) {
    return abep::DensityTable({{100.0e3, rho}, {1000.0e3, rho}});
}

// Time to come down from a0 to a_stop on a circular orbit in this atmosphere, from da/dt = -(Cd A / m) rho sqrt(GM a): the energy
// lost to drag, 1/2 rho v^2 (Cd A / m) times v, set equal to the rate of change of -GM / 2a. Integrated 1/(da/dt) by Simpson's
// rule over a, which does not use the propagator at all.
double reference_decay_time(const abep::DensityTable& table, double cda_over_m, double a0, double a_stop) {
    const auto inverse_rate = [&](double a) {
        const double rho = table.density(a - abep::constants::kEarthEquatorialRadius);
        return 1.0 / (cda_over_m * rho * std::sqrt(abep::constants::kEarthGM * a));
    };
    const int n = 20000;  // even
    const double h = (a0 - a_stop) / static_cast<double>(n);
    double sum = inverse_rate(a_stop) + inverse_rate(a0);
    for (int i = 1; i < n; ++i) sum += inverse_rate(a_stop + h * static_cast<double>(i)) * (i % 2 == 1 ? 4.0 : 2.0);
    return sum * h / 3.0;
}

}  // namespace

void runSummaryTests() {
    {
        TEST("summary: a stop altitude ends the run when the satellite falls below it");
        const abep::Satellite sat = satellite();
        abep::Scenario sc = scenario(150.0e3, 0.0, 86400.0);
        sc.thruster_enabled = false;
        sc.stop_below_altitude_m = 140.0e3;
        const auto run = abep::run_mission(sat, sc, abep::ControllerConfig{}, ussa());
        CHECK(run.end_reason == abep::EndReason::BelowStopAltitude);
        CHECK(abep::altitude_above_ellipsoid(run.final_state.r) < 140.0e3);
        CHECK(!run.samples.empty());
        if (!run.samples.empty()) CHECK(run.samples.back().altitude_m >= 140.0e3);  // the step before was still above
        CHECK(run.elapsed_s > 0.0 && run.elapsed_s < 86400.0);
        sc.stop_below_altitude_m = 0.0;  // 0 means never
        CHECK(abep::run_mission(sat, sc, abep::ControllerConfig{}, ussa()).end_reason != abep::EndReason::BelowStopAltitude);
        CHECK_THROWS(([&] {
                         abep::Scenario bad = sc;
                         bad.stop_below_altitude_m = -1.0;
                         return abep::run_mission(sat, bad, abep::ControllerConfig{}, ussa());
                     }()),
                     std::invalid_argument);
        CHECK_THROWS(([&] {
                         abep::Scenario bad = sc;
                         bad.stop_below_altitude_m = std::numeric_limits<double>::quiet_NaN();
                         return abep::run_mission(sat, bad, abep::ControllerConfig{}, ussa());
                     }()),
                     std::invalid_argument);
        CHECK(std::string(abep::end_reason_name(abep::EndReason::Completed)) == "Completed");
        CHECK(std::string(abep::end_reason_name(abep::EndReason::AltitudeOutOfRange)) == "AltitudeOutOfRange");
        CHECK(std::string(abep::end_reason_name(abep::EndReason::BelowStopAltitude)) == "BelowStopAltitude");
    }
    {
        TEST("decay: the time to decay agrees with the circular-orbit decay equation, which does not use the propagator");
        // An equatorial circular orbit in an exactly exponential atmosphere (rho 2.5e-10 kg/m^3 at 200 km, scale height 45 km),
        // two-body gravity, air that does not turn, from 200 km down to 120 km. Tolerance, written before the run: 1%. The
        // equation treats the orbit as circular all the way down and the propagator does not, and the run notices the crossing at
        // the start of a 10 s step, a few seconds late in some 10^5 s.
        const abep::DensityTable table = exponential_table(2.5e-10, 200.0e3, 45.0e3);
        const abep::Satellite sat = satellite();
        const abep::Scenario sc = scenario(200.0e3, 0.0, 1.0);
        const double cda_over_m = sat.drag_coefficient * sat.frontal_area_m2 / sat.mass_kg;
        const double a0 = abep::semi_major_axis(sc.initial);
        const double reference = reference_decay_time(table, cda_over_m, a0, abep::constants::kEarthEquatorialRadius + 120.0e3);
        const abep::DecayResult d = abep::time_to_decay(sat, sc, table, 120.0e3, 60.0 * 86400.0);
        CHECK(d.decayed);
        CHECK(d.final_altitude_m < 120.0e3);
        CHECK(d.final_altitude_m > 119.0e3);
        const double error = (d.time_s - reference) / reference;
        CHECK(std::fabs(error) <= 0.01);
        std::printf("    200 km to 120 km: %.4f days propagated, %.4f days from the equation (%+.3f%%)\n", d.time_s / 86400.0,
                    reference / 86400.0, 100.0 * error);

        // Time to decay is proportional to m / (Cd A): twice the mass, twice the time, in the same atmosphere.
        abep::Satellite heavy = sat;
        heavy.mass_kg = 2.0 * sat.mass_kg;
        const abep::DecayResult dh = abep::time_to_decay(heavy, sc, table, 120.0e3, 120.0 * 86400.0);
        CHECK(dh.decayed);
        CHECK(std::fabs(dh.time_s / d.time_s - 2.0) <= 0.01);
    }
    {
        TEST("decay: a satellite that does not come down in the time allowed says so, and one that starts below has already");
        const abep::Satellite sat = satellite();
        const abep::DecayResult stays = abep::time_to_decay(sat, scenario(400.0e3, 51.6, 1.0), ussa(), 0.0, 86400.0);
        CHECK(!stays.decayed);
        CHECK_EQ(stays.time_s, 86400.0);
        CHECK(stays.final_altitude_m > 395.0e3 && stays.final_altitude_m < 425.0e3);  // oblate Earth: 400 km at the equator, up to 413 km high
        const abep::DecayResult below = abep::time_to_decay(sat, scenario(200.0e3, 0.0, 1.0), ussa(), 250.0e3, 86400.0);
        CHECK(below.decayed);
        CHECK_EQ(below.time_s, 0.0);
        // A decay altitude below the density table is the lower edge of the table, 100 km, where the run has to stop anyway.
        const abep::DecayResult edge = abep::time_to_decay(sat, scenario(110.0e3, 0.0, 1.0), ussa(), 50.0e3, 86400.0);
        CHECK(edge.decayed);
        // It stops within a step of the edge. At 100 km a satellite like this one falls about 6 km in a 10 s step (the decay
        // equation gives 600 m/s there), so the last state kept can be that much above the edge, as in the CLI's default run.
        CHECK(edge.final_altitude_m < 110.0e3);
        CHECK_THROWS(abep::time_to_decay(sat, scenario(200.0e3, 0.0, 1.0), ussa(), -1.0, 86400.0), std::invalid_argument);
        CHECK_THROWS(abep::time_to_decay(sat, scenario(200.0e3, 0.0, 1.0), ussa(), 0.0, 0.0), std::invalid_argument);
        CHECK_THROWS(abep::time_to_decay(sat, scenario(200.0e3, 0.0, 1.0), ussa(), std::numeric_limits<double>::infinity(), 86400.0),
                     std::invalid_argument);
        // The 1976 standard atmosphere, for the record: where the real table puts a satellite like this one.
        const abep::DecayResult real = abep::time_to_decay(sat, scenario(250.0e3, 51.6, 1.0), ussa(), 0.0, 10.0 * abep::kSecondsPerYear);
        CHECK(real.decayed);
        CHECK(real.time_s > 86400.0 && real.time_s < 60.0 * 86400.0);
        std::printf("    250 km, 100 kg, 1 m^2, Cd 2.2, standard atmosphere: down to 100 km after %.1f days\n", real.time_s / 86400.0);
    }
    {
        TEST("hold: with the same density and speed all round the orbit, the power is the drag over the thrust density");
        // An equatorial circular orbit at 400 km in a uniform 1e-12 kg/m^3, two-body gravity, air that does not turn: the drag is
        // 1/2 rho v^2 Cd A with v = sqrt(GM / r) at every step, and an intake of 1000 m^2 means the power sets the thrust. The
        // orbit does fall about 6 m while it is averaged over, which changes the drag by about 1e-6, so 1e-4 (written before the run).
        const abep::DensityTable table = uniform_table(1.0e-12);
        const abep::Satellite sat = satellite();
        const abep::Scenario sc = scenario(400.0e3, 0.0, 1.0);
        const double r = abep::constants::kEarthEquatorialRadius + 400.0e3;
        const double v2 = abep::constants::kEarthGM / r;
        const double drag = 0.5 * 1.0e-12 * v2 * sat.drag_coefficient * sat.frontal_area_m2;
        const abep::HoldResult h = abep::power_to_hold_altitude(sat, sc, table);
        CHECK(h.status == abep::HoldResult::Status::Holds);
        CHECK(std::fabs(h.mean_drag_n - drag) <= 1.0e-4 * drag);
        CHECK(std::fabs(h.power_w - drag / sat.thruster.thrust_density_n_per_w) <= 1.0e-4 * h.power_w);
        CHECK(h.mean_intake_limit_n > 10.0 * h.mean_drag_n);
        CHECK(std::string(abep::hold_status_name(h.status)) == "Holds");
        std::printf("    drag %.4e N, power %.2f W (thrust density %.1f mN/kW)\n", h.mean_drag_n, h.power_w,
                    units::n_per_w_to_mn_per_kw(sat.thruster.thrust_density_n_per_w));
    }
    {
        TEST("hold: whether the intake can supply the drag does not depend on the density, and the answer turns over where it should");
        // The ratio of the thrust the intake supports to the drag is eta_c A_in c_e / (1/2 Cd A v_rel): no density in it. In the same
        // uniform atmosphere, an intake 1% larger than the one that makes the ratio 1 holds the altitude and one 1% smaller does not.
        const abep::DensityTable table = uniform_table(1.0e-12);
        const abep::Scenario sc = scenario(400.0e3, 0.0, 1.0);
        abep::Satellite sat = satellite();
        const double v = std::sqrt(abep::constants::kEarthGM / (abep::constants::kEarthEquatorialRadius + 400.0e3));
        const double a_hold = 0.5 * sat.drag_coefficient * sat.frontal_area_m2 * v / (sat.thruster.capture_efficiency * sat.thruster.exhaust_velocity_m_s);
        sat.thruster.intake_area_m2 = 1.01 * a_hold;
        const abep::HoldResult more = abep::power_to_hold_altitude(sat, sc, table);
        CHECK(more.status == abep::HoldResult::Status::Holds);
        CHECK(std::isfinite(more.power_w));
        CHECK(std::fabs(more.mean_intake_limit_n / more.mean_drag_n - 1.01) <= 1.0e-4);
        sat.thruster.intake_area_m2 = 0.99 * a_hold;
        const abep::HoldResult less = abep::power_to_hold_altitude(sat, sc, table);
        CHECK(less.status == abep::HoldResult::Status::IntakeLimited);
        CHECK(std::isinf(less.power_w));
        CHECK(std::isinf(less.array_power_w));
        CHECK(std::fabs(less.mean_intake_limit_n / less.mean_drag_n - 0.99) <= 1.0e-4);
        CHECK(std::string(abep::hold_status_name(less.status)) == "IntakeLimited");
        // A thicker atmosphere changes nothing about that.
        const abep::HoldResult thick = abep::power_to_hold_altitude(sat, sc, uniform_table(1.0e-9));
        CHECK(thick.status == abep::HoldResult::Status::IntakeLimited);
        sat.thruster.intake_area_m2 = 0.0;  // no intake at all
        CHECK(abep::power_to_hold_altitude(sat, sc, table).status == abep::HoldResult::Status::IntakeLimited);
    }
    {
        TEST("hold: where the intake limits some steps and not others, the thrust over the orbit still equals the drag");
        // The 1976 atmosphere, 250 km at 51.6 deg with J2 and the air turning, so the density and the relative speed change round the
        // orbit and, with the intake set to a ratio of 1.02 on average, the cap binds on part of it. Checked against a direct sum over
        // the steps of an independent run with the thrust model's own min(): relative 1e-9 (a sum of 550 terms).
        abep::Satellite sat = satellite();
        abep::Scenario sc = scenario(250.0e3, 51.6, 1.0);
        sc.include_j2 = true;
        sc.corotating_atmosphere = true;
        const abep::HoldResult first = abep::power_to_hold_altitude(sat, sc, ussa());
        sat.thruster.intake_area_m2 *= 1.02 * first.mean_drag_n / first.mean_intake_limit_n;
        const abep::HoldResult h = abep::power_to_hold_altitude(sat, sc, ussa());
        CHECK(h.status == abep::HoldResult::Status::Holds);
        CHECK(std::fabs(h.mean_intake_limit_n / h.mean_drag_n - 1.02) <= 1.0e-9);

        abep::Scenario one_orbit = sc;
        one_orbit.thruster_enabled = false;
        one_orbit.duration_s = abep::orbital_period_two_body(abep::semi_major_axis(sc.initial));
        one_orbit.sample_every = 1;
        const auto run = abep::run_mission(sat, one_orbit, abep::ControllerConfig{}, ussa());
        double drag_ns = 0.0;
        double thrust_ns = 0.0;
        long capped = 0;
        long free_steps = 0;
        for (const abep::Sample& s : run.samples) {
            const double w = std::min(one_orbit.step_s, one_orbit.duration_s - s.t_s);
            const abep::ThrustResult t = abep::abep_thrust(sat.thruster, h.power_w, s.density_kg_m3, s.relative_speed_m_s);
            drag_ns += w * s.drag_n;
            thrust_ns += w * t.thrust_n;
            if (t.limit == abep::ThrustLimit::Propellant) ++capped; else ++free_steps;
        }
        CHECK(std::fabs(thrust_ns - drag_ns) <= 1.0e-9 * drag_ns);
        CHECK(capped > 0 && free_steps > 0);  // both branches of the solution were used
        std::printf("    %ld of %zu steps held at the intake's limit; power %.1f W\n", capped, run.samples.size(), h.power_w);
    }
    {
        TEST("hold: thrusting at that power keeps the semi-major axis, and a tenth less or more lets it fall or rise");
        // Equatorial circular orbit at 300 km in the 1976 atmosphere, J2 out, the air turning (drag is then along the velocity, so
        // thrust against it balances exactly), a battery and an array big enough that the thruster runs all the time, in eclipse too.
        // Ten orbits. Drag alone lowers the orbit by about 1.2 km; at the held power the change must be under 3% of that.
        abep::Satellite sat = satellite();
        sat.array_power_w = 1.0e5;
        sat.battery_capacity_wh = 1.0e6;
        sat.battery_initial_wh = 0.9e6;
        abep::Scenario sc = scenario(300.0e3, 0.0, 1.0);
        sc.corotating_atmosphere = true;
        const double period = abep::orbital_period_two_body(abep::semi_major_axis(sc.initial));
        sc.duration_s = 10.0 * period;
        const abep::HoldResult h = abep::power_to_hold_altitude(sat, sc, ussa());
        CHECK(h.status == abep::HoldResult::Status::Holds);

        abep::ControllerConfig cfg;
        cfg.thrust_in_eclipse = true;
        const double a0 = abep::semi_major_axis(sc.initial);
        abep::Scenario drag_only = sc;
        drag_only.thruster_enabled = false;
        const double lost = a0 - abep::semi_major_axis(abep::run_mission(sat, drag_only, cfg, ussa()).final_state);
        CHECK(lost > 500.0);

        const auto change_at = [&](double fraction) {
            abep::Satellite s = sat;
            s.thruster_power_w = fraction * h.power_w;
            const auto run = abep::run_mission(s, sc, cfg, ussa());
            CHECK(run.seconds_in(abep::Mode::Thrust) > 0.99 * run.elapsed_s);
            return abep::semi_major_axis(run.final_state) - a0;
        };
        const double held = change_at(1.0);
        CHECK(std::fabs(held) <= 0.03 * lost);
        CHECK(change_at(0.9) < -0.05 * lost);   // a tenth short: it falls, by about a tenth of what drag alone costs
        CHECK(change_at(1.1) > 0.05 * lost);    // a tenth over: it rises
        std::printf("    drag alone: %.0f m of semi-major axis in 10 orbits; at %.1f W: %+.1f m; at 90%%: %+.0f m; at 110%%: %+.0f m\n",
                    lost, h.power_w, held, change_at(0.9), change_at(1.1));
    }
    {
        TEST("hold: the array that carries the thruster and the bus round an orbit is their sum over the sunlit share");
        abep::Satellite sat = satellite();
        sat.bus_load_w = 250.0;
        const abep::Scenario sc = scenario(300.0e3, 0.0, 1.0);  // beta 0: the longest shadow
        const abep::HoldResult h = abep::power_to_hold_altitude(sat, sc, ussa());
        CHECK(h.status == abep::HoldResult::Status::Holds);
        const double r = abep::constants::kEarthEquatorialRadius + 300.0e3;
        const double eclipse = abep::circular_orbit_eclipse_fraction(r, 0.0);
        // Sampled every 10 s of an orbit of about 5400 s, so the sunlit share is right to a step, 2e-3 of an orbit.
        CHECK(std::fabs(h.sunlit_fraction - (1.0 - eclipse)) <= 2.0e-3);
        CHECK(std::fabs(h.array_power_w * h.sunlit_fraction - (h.power_w + 250.0)) <= 1.0e-9 * h.array_power_w);
        std::printf("    eclipse %.3f of the orbit; %.1f W of thruster and 250 W of bus ask %.1f W of array in sunlight\n", eclipse,
                    h.power_w, h.array_power_w);
    }
    {
        TEST("hold: inputs that make no sense are refused");
        const abep::Satellite sat = satellite();
        // Below the density table there is no orbit to average over.
        CHECK_THROWS(abep::power_to_hold_altitude(sat, scenario(50.0e3, 0.0, 1.0), ussa()), std::invalid_argument);
        abep::Satellite bad = sat;
        bad.mass_kg = 0.0;
        CHECK_THROWS(abep::power_to_hold_altitude(bad, scenario(300.0e3, 0.0, 1.0), ussa()), std::invalid_argument);
        abep::Scenario sc = scenario(300.0e3, 0.0, 1.0);
        sc.step_s = 0.0;
        CHECK_THROWS(abep::power_to_hold_altitude(sat, sc, ussa()), std::invalid_argument);
    }
}
