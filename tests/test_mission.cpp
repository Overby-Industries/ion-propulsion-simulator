#include "check.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <vector>

#include "abep/atmosphere.hpp"
#include "abep/drag.hpp"
#include "abep/eclipse.hpp"
#include "abep/flight.hpp"
#include "abep/mission.hpp"
#include "abep/orbit.hpp"
#include "abep/power.hpp"
#include "abep/standard_atmosphere_1976.hpp"
#include "abep/thermal.hpp"
#include "abep/thrust.hpp"
#include "abep/units.hpp"

namespace {

using abep::Mode;
namespace units = abep::units;

const abep::DensityTable& atmosphere() {
    static const abep::DensityTable table = abep::ussa1976::density_table();
    return table;
}

// An illustrative satellite, not data about any real one. Thrust density 13.7 mN/kW and exhaust velocity 20 km/s (an implied
// thruster efficiency of 13.7%); an intake of 10 m^2 at 50% so that, at the altitudes used here, the air does not
// limit the thrust unless a test says it should.
abep::Satellite example_satellite() {
    abep::Satellite s;
    s.mass_kg = 2000.0;
    s.frontal_area_m2 = 1.0;
    s.drag_coefficient = 2.2;
    s.thruster = {units::mn_per_kw_to_n_per_w(13.7), 20.0e3, 10.0, 0.5};
    s.thruster_power_w = 2000.0;
    s.array_power_w = 2500.0;
    s.bus_load_w = 500.0;
    s.safe_load_w = 100.0;
    s.battery_capacity_wh = 1000.0;
    s.battery_initial_wh = 600.0;
    s.initial_temperature_k = 300.0;
    return s;
}

// A circular orbit with the Sun along +x. For an inclination of 90 deg the orbit normal is (sin(raan), -cos(raan), 0), so
// the beta angle equals the node's right ascension, and J2 does not turn the plane.
abep::Scenario example_scenario(double altitude_m, double inclination_deg, double raan_deg, double duration_s) {
    abep::Scenario sc;
    sc.initial = abep::circular_orbit_state(altitude_m, units::deg_to_rad(inclination_deg), units::deg_to_rad(raan_deg), 0.0);
    sc.sun_unit = abep::sun_direction_from_ra_dec(0.0, 0.0);
    sc.duration_s = duration_s;
    sc.step_s = 10.0;
    return sc;
}

// The modes in order, each run of equal modes collapsed to one entry, with the time the run began.
struct Segment {
    Mode mode;
    double start_s;
};

std::vector<Segment> segments(const abep::MissionResult& r) {
    std::vector<Segment> out;
    for (const abep::Sample& s : r.samples) {
        if (out.empty() || out.back().mode != s.mode) out.push_back({s.mode, s.t_s});
    }
    return out;
}

bool sequence_is(const std::vector<Segment>& got, std::initializer_list<Mode> want) {
    if (got.size() != want.size()) return false;
    std::size_t i = 0;
    for (const Mode m : want) {
        if (got[i++].mode != m) return false;
    }
    return true;
}

double start_of(const std::vector<Segment>& segs, std::size_t index) {
    return index < segs.size() ? segs[index].start_s : -1.0;
}

}  // namespace

void runMissionTests() {
    {
        TEST("mission: inputs that make no sense are refused");
        const abep::ControllerConfig cfg;
        const abep::Satellite good = example_satellite();
        const abep::Scenario scenario = example_scenario(250.0e3, 90.0, 30.0, 100.0);
        const double nan = std::numeric_limits<double>::quiet_NaN();
        CHECK(!abep::run_mission(good, scenario, cfg, atmosphere()).samples.empty());  // the unchanged inputs are fine
        {
            abep::Satellite s = good;
            s.mass_kg = 0.0;
            CHECK_THROWS(abep::run_mission(s, scenario, cfg, atmosphere()), std::invalid_argument);
        }
        {
            abep::Satellite s = good;
            s.frontal_area_m2 = -1.0;
            CHECK_THROWS(abep::run_mission(s, scenario, cfg, atmosphere()), std::invalid_argument);
        }
        {
            abep::Satellite s = good;
            s.drag_coefficient = 0.0;
            CHECK_THROWS(abep::run_mission(s, scenario, cfg, atmosphere()), std::invalid_argument);
        }
        {
            abep::Satellite s = good;
            s.thruster.capture_efficiency = 2.0;
            CHECK_THROWS(abep::run_mission(s, scenario, cfg, atmosphere()), std::invalid_argument);
        }
        {
            abep::Satellite s = good;
            s.thruster_power_w = nan;
            CHECK_THROWS(abep::run_mission(s, scenario, cfg, atmosphere()), std::invalid_argument);
        }
        {
            abep::Satellite s = good;
            s.array_power_w = -1.0;
            CHECK_THROWS(abep::run_mission(s, scenario, cfg, atmosphere()), std::invalid_argument);
        }
        {
            abep::Satellite s = good;
            s.battery_initial_wh = s.battery_capacity_wh + 1.0;
            CHECK_THROWS(abep::run_mission(s, scenario, cfg, atmosphere()), std::invalid_argument);
        }
        {
            abep::Satellite s = good;
            s.initial_temperature_k = 0.0;
            CHECK_THROWS(abep::run_mission(s, scenario, cfg, atmosphere()), std::invalid_argument);
        }
        {
            abep::Satellite s = good;
            s.thermal = abep::ThrusterThermal{0.0, 5.0, 300.0};
            CHECK_THROWS(abep::run_mission(s, scenario, cfg, atmosphere()), std::invalid_argument);
        }
        {
            abep::Scenario sc = scenario;
            sc.duration_s = -1.0;
            CHECK_THROWS(abep::run_mission(good, sc, cfg, atmosphere()), std::invalid_argument);
        }
        {
            abep::Scenario sc = scenario;
            sc.step_s = 0.0;
            CHECK_THROWS(abep::run_mission(good, sc, cfg, atmosphere()), std::invalid_argument);
        }
        {
            abep::Scenario sc = scenario;
            sc.sample_every = 0;
            CHECK_THROWS(abep::run_mission(good, sc, cfg, atmosphere()), std::invalid_argument);
        }
        {
            abep::Scenario sc = scenario;
            sc.sun_unit = {2.0, 0.0, 0.0};
            CHECK_THROWS(abep::run_mission(good, sc, cfg, atmosphere()), std::invalid_argument);
        }
        {
            abep::Scenario sc = scenario;
            sc.array_outages = {{50.0, 10.0}};  // ends before it starts
            CHECK_THROWS(abep::run_mission(good, sc, cfg, atmosphere()), std::invalid_argument);
        }
        {
            abep::Scenario sc = scenario;
            sc.initial.v = 1.5 * sc.initial.v;  // faster than escape from this radius
            CHECK_THROWS(abep::run_mission(good, sc, cfg, atmosphere()), std::invalid_argument);
        }
        {
            abep::ControllerConfig c;
            c.soc_thrust_stop = 0.6;
            CHECK_THROWS(abep::run_mission(good, scenario, c, atmosphere()), std::invalid_argument);
        }
    }
    {
        TEST("mission: with the thruster off it is the same physics as propagating gravity and drag directly");
        // The run and the direct propagation do the same operations in the same order, so without fused multiply-add they
        // agree bit for bit. Where the compiler fuses them differently in the two places the results differ by rounding:
        // measured over nine compiler settings, up to 2.6e-6 m and 3.1e-9 m/s after these 5000 steps. The first version of
        // this test asked for 1e-6 m and 1e-9 m/s, which is at that noise, and failed in the -march=native build
        // (docs/VALIDATION.md, finding 14). Now 1e-3 m and 1e-6 m/s: 400 times the noise, and still tight enough to catch
        // an error of 1e-7 in the drag (drag moves the satellite by about 4e4 m over this run).
        abep::Satellite sat = example_satellite();
        sat.mass_kg = 500.0;
        abep::Scenario sc = example_scenario(200.0e3, 90.0, 30.0, 50000.0);
        sc.thruster_enabled = false;
        const auto run = abep::run_mission(sat, sc, abep::ControllerConfig{}, atmosphere());
        const auto accel = [&](double, const abep::State& st) {
            const double rho = atmosphere().density(abep::altitude_above_ellipsoid(st.r));
            return abep::gravity_acceleration(st.r, true) +
                   abep::drag_acceleration(st, rho, sat.drag_coefficient, sat.frontal_area_m2, sat.mass_kg, true);
        };
        const abep::State direct = abep::propagate(sc.initial, 0.0, 50000.0, 10.0, accel);
        CHECK(abep::norm(run.final_state.r - direct.r) <= 1.0e-3);
        CHECK(abep::norm(run.final_state.v - direct.v) <= 1.0e-6);
        CHECK(run.end_reason == abep::EndReason::Completed);
        CHECK_EQ(run.elapsed_s, 50000.0);
        CHECK_EQ(run.seconds_in(Mode::Thrust), 0.0);  // never commanded
        // The sample's relative speed is the library's own relative velocity at the start. It is the same expression as the
        // test's, evaluated in two places, so it agrees bit for bit only where the compiler fuses the multiply-adds of
        // `v + w * y` the same way in both. With GCC 16.2 at -O2 or -O3 and -mfma it does not: the two differ by one unit in
        // the last place, 9.1e-13 m/s of 7799 m/s, and the Ubuntu job of the CI failed on a bit-exact check of this
        // (docs/VALIDATION.md, finding 17). Now 1e-9 m/s, 1000 times that, and still far finer than any change to the model:
        // leaving out Earth's rotation moves this speed by about 15 m/s, and a relative error of 1e-9 in the rotation rate
        // by 3e-8 m/s. The drag below is a product of the stored numbers with no addition to fuse, so it is exact.
        CHECK(!run.samples.empty());
        if (!run.samples.empty()) {
            const abep::Sample& first = run.samples.front();
            CHECK_NEAR(first.relative_speed_m_s, abep::norm(abep::velocity_relative_to_air(sc.initial, true)), 1.0e-9);
            CHECK_EQ(first.drag_n, abep::drag_force(first.density_kg_m3, first.relative_speed_m_s, sat.drag_coefficient, sat.frontal_area_m2));
            // The air moves with the Earth, so the relative speed is not the inertial one (for this polar orbit it is higher:
            // the air's eastward 0.4 km/s is across the satellite's northward 7.8 km/s).
            CHECK(first.relative_speed_m_s > abep::norm(sc.initial.v));
        }
        long drag_wrong = 0;
        for (const abep::Sample& s : run.samples) {
            if (s.drag_n != abep::drag_force(s.density_kg_m3, s.relative_speed_m_s, sat.drag_coefficient, sat.frontal_area_m2)) ++drag_wrong;
        }
        CHECK_EQ(drag_wrong, 0L);
        // Drag takes energy out of the orbit: without J2 the energy is otherwise conserved, and 9 orbits at 200 km for
        // this satellite cost about 2.7 km of semi-major axis, 12 kJ/kg (case 5's per-orbit formula); the bound is 5 kJ/kg.
        abep::Scenario two_body = sc;
        two_body.include_j2 = false;
        const auto lost = abep::run_mission(sat, two_body, abep::ControllerConfig{}, atmosphere());
        CHECK(abep::specific_energy(lost.final_state) < abep::specific_energy(sc.initial) - 5.0e3);
        std::printf("    drag took %.1f kJ/kg out of the orbit in 50000 s\n",
                    1.0e-3 * (abep::specific_energy(sc.initial) - abep::specific_energy(lost.final_state)));
    }
    {
        TEST("mission: the run stops where the density table stops, and says so");
        abep::Satellite sat = example_satellite();
        abep::Scenario sc = example_scenario(101.0e3, 90.0, 30.0, 20000.0);
        sc.thruster_enabled = false;
        const auto decayed = abep::run_mission(sat, sc, abep::ControllerConfig{}, atmosphere());
        CHECK(decayed.end_reason == abep::EndReason::AltitudeOutOfRange);
        CHECK(decayed.elapsed_s > 0.0 && decayed.elapsed_s < 20000.0);
        CHECK(!decayed.samples.empty());
        CHECK(decayed.samples.back().altitude_m >= atmosphere().min_altitude_m());  // the last sample is a valid one
        abep::Scenario below = example_scenario(50.0e3, 90.0, 30.0, 1000.0);  // never in the table
        const auto none = abep::run_mission(sat, below, abep::ControllerConfig{}, atmosphere());
        CHECK(none.end_reason == abep::EndReason::AltitudeOutOfRange);
        CHECK_EQ(none.elapsed_s, 0.0);
        CHECK(none.samples.empty());
        std::printf("    from 101 km the satellite left the table after %.0f s\n", decayed.elapsed_s);
    }
    {
        TEST("mission: where the intake cannot supply the air, the thrust is the air's and the power drawn falls short");
        // At 400 km the 1976 density is about 3e-12 kg/m^3, so a 1 m^2 intake at 35% collects a few nanograms a second and
        // can make well under a millinewton at 20 km/s, against the 27 mN that 2000 W would give. Checked step by step
        // against the thrust model (exact, a few unit roundoffs): the thrust is the collectable mass flow times the exhaust
        // velocity, the power drawn is that thrust over the thrust density, and both are far below the power limit.
        abep::Satellite sat = example_satellite();
        sat.thruster = {units::mn_per_kw_to_n_per_w(13.7), 20.0e3, 1.0, 0.35};
        abep::Scenario sc = example_scenario(400.0e3, 90.0, 80.0, 3000.0);
        const auto run = abep::run_mission(sat, sc, abep::ControllerConfig{}, atmosphere());
        long thrust_samples = 0;
        long thrust_wrong = 0;
        long power_wrong = 0;
        long not_air_limited = 0;
        for (const abep::Sample& s : run.samples) {
            if (s.mode != Mode::Thrust) continue;
            ++thrust_samples;
            const double expected = abep::collectable_mass_flow(sat.thruster, s.density_kg_m3, s.relative_speed_m_s) * sat.thruster.exhaust_velocity_m_s;
            if (std::fabs(s.thrust_n - expected) > 1.0e-13 * expected) ++thrust_wrong;
            if (std::fabs(s.thruster_power_w - expected / sat.thruster.thrust_density_n_per_w) > 1.0e-12) ++power_wrong;
            if (!(s.thrust_n < 1.0e-3 && s.thruster_power_w < 0.1 * sat.thruster_power_w)) ++not_air_limited;
        }
        CHECK(thrust_samples > 100);
        CHECK_EQ(thrust_wrong, 0L);
        CHECK_EQ(power_wrong, 0L);
        CHECK_EQ(not_air_limited, 0L);
    }
    {
        TEST("mission: a duration that is not a whole number of steps ends exactly on time");
        // 105 s in steps of 10 s: ten steps and one of 5 s. The array gives 2500 W throughout (a beta of 80 deg never sees
        // the shadow), so the energy generated is exactly 2500 W for 105 s.
        const abep::Satellite sat = example_satellite();
        const abep::Scenario sc = example_scenario(250.0e3, 90.0, 80.0, 105.0);
        const auto run = abep::run_mission(sat, sc, abep::ControllerConfig{}, atmosphere());
        CHECK_EQ(run.elapsed_s, 105.0);
        CHECK_EQ(run.samples.size(), static_cast<std::size_t>(11));
        CHECK(!run.samples.empty() && run.samples.back().t_s == 100.0);
        CHECK(run.orbits.size() == 1);
        if (run.orbits.size() == 1) {
            CHECK_EQ(run.orbits[0].duration_s, 105.0);
            CHECK_NEAR(run.orbits[0].generated_wh, 2500.0 * 105.0 / 3600.0, 1.0e-9);
        }
    }
    {
        TEST("mission: 40 orbits, thrusting in sunlight only: the books balance and the duty matches the power budget");
        // Tolerances fixed before running.
        //  - Energy bookkeeping: the per-orbit books equal the totals recomputed from the samples to 1e-9 relative (a few
        //    thousand additions in a different order, about 1e-13), and the battery's change equals the sum of the changes
        //    it reported, and the spill is nil, to 1e-6 Wh: 21,000 terms of about a watt-hour each could add up to 3e-9 Wh
        //    of rounding even if every one went the same way.
        //  - Duty: within capacity / (thruster power x time) of the closed form, evaluated with the run's own sunlit share.
        //    That is a bound and not a fit: the energy balance says the two differ only by the battery's net change over
        //    the run, which cannot exceed its capacity, and 1200 Wh is 1.0% of what the thruster would use at full power
        //    in these 59.7 h (2000 W).
        //  - Eclipse: 0.002 of an orbit against the closed form (case 10 found 1.4e-4 for a propagated orbit; the radius
        //    here moves a few km).
        //  - The battery never falls below the thrust-stop level (20%): the eclipse costs 300 Wh, a quarter of the battery,
        //    and thrusting restarts only at 50%.
        // Parameters: array 2500 W, load 500 W, thruster 2000 W, so the closed-form duty is 0.50, below the sunlit share
        // of 0.60 that sunlit-only thrusting can reach. The satellite starts in SAFE, and the first step already moves it
        // to CHARGE, so the first sample is CHARGE and the second is THRUST.
        abep::Satellite sat = example_satellite();
        sat.battery_capacity_wh = 1200.0;
        sat.battery_initial_wh = 720.0;
        abep::Scenario sc = example_scenario(250.0e3, 90.0, 30.0, 0.0);
        const double period = abep::orbital_period_two_body(abep::norm(sc.initial.r));
        sc.duration_s = std::floor(40.0 * period / sc.step_s) * sc.step_s;
        const abep::ControllerConfig cfg;  // the defaults: sunlit-only thrusting
        const auto run = abep::run_mission(sat, sc, cfg, atmosphere());

        CHECK(run.end_reason == abep::EndReason::Completed);
        CHECK_EQ(run.elapsed_s, sc.duration_s);
        CHECK_EQ(run.samples.size(), static_cast<std::size_t>(sc.duration_s / sc.step_s));

        long outside_battery = 0;
        long thrust_outside_thrust_mode = 0;
        long power_not_thrust_over_tau = 0;
        long net_drag_wrong = 0;
        long load_wrong = 0;
        double generated_wh = 0.0;
        double load_wh = 0.0;
        double thruster_wh = 0.0;
        double lowest_wh = sat.battery_capacity_wh;
        for (const abep::Sample& s : run.samples) {
            lowest_wh = std::min(lowest_wh, s.battery_wh);
            if (!(s.battery_wh >= 0.0 && s.battery_wh <= sat.battery_capacity_wh)) ++outside_battery;
            if (s.mode != Mode::Thrust && (s.thrust_n != 0.0 || s.thruster_power_w != 0.0)) ++thrust_outside_thrust_mode;
            if (std::fabs(s.thruster_power_w - s.thrust_n / sat.thruster.thrust_density_n_per_w) > 1.0e-9 * sat.thruster_power_w) {
                ++power_not_thrust_over_tau;
            }
            if (s.net_drag_n != s.drag_n - s.thrust_n) ++net_drag_wrong;
            const double expected_load = (s.mode == Mode::Safe || s.mode == Mode::Fault) ? sat.safe_load_w : sat.bus_load_w;
            if (s.load_w != expected_load) ++load_wrong;
            generated_wh += s.generation_w * sc.step_s / 3600.0;
            load_wh += s.load_w * sc.step_s / 3600.0;
            thruster_wh += s.thruster_power_w * sc.step_s / 3600.0;
        }
        CHECK_EQ(outside_battery, 0L);
        CHECK_EQ(thrust_outside_thrust_mode, 0L);
        CHECK_EQ(power_not_thrust_over_tau, 0L);
        CHECK_EQ(net_drag_wrong, 0L);
        CHECK_EQ(load_wrong, 0L);

        double books_generated = 0.0;
        double books_load = 0.0;
        double books_thruster = 0.0;
        double books_change = 0.0;
        double books_spilled = 0.0;
        double books_eclipse = 0.0;
        double books_time = 0.0;
        double books_thrust_s = 0.0;
        for (const abep::OrbitBalance& o : run.orbits) {
            books_thrust_s += o.thrust_s;
            books_generated += o.generated_wh;
            books_load += o.load_wh;
            books_thruster += o.thruster_wh;
            books_change += o.battery_change_wh;
            books_spilled += o.spilled_wh;
            books_eclipse += o.eclipse_s;
            books_time += o.duration_s;
        }
        CHECK_NEAR(books_generated, generated_wh, 1.0e-9 * generated_wh);
        CHECK_NEAR(books_load, load_wh, 1.0e-9 * load_wh);
        CHECK_NEAR(books_thruster, thruster_wh, 1.0e-9 * thruster_wh);
        CHECK_NEAR(books_time, run.elapsed_s, 1.0e-9);
        CHECK_NEAR(books_thrust_s, run.seconds_in(Mode::Thrust), 1.0e-9);
        CHECK_NEAR(books_change, run.final_battery_wh - sat.battery_initial_wh, 1.0e-6);
        CHECK_NEAR(books_spilled, 0.0, 1.0e-6);  // the battery was never full and never empty
        double mode_total = 0.0;
        for (const double seconds : run.seconds_in_mode) mode_total += seconds;
        CHECK_NEAR(mode_total, run.elapsed_s, 1.0e-9);
        CHECK(run.orbits.size() == 40 || run.orbits.size() == 41);
        // Every orbit repeats the first one after the start-up: the duty of a middle orbit, 5% of duty either way of the
        // whole run's (the recharge after the shadow is the same each time; this is a loose bound on a repeating cycle).
        CHECK(run.orbits.size() > 20);
        if (run.orbits.size() > 20) CHECK_NEAR(run.orbits[20].duty(), run.thrust_duty(), 0.05);

        // The modes: CHARGE for one step at the start, then thrust in sunlight and coast in the shadow, never SAFE or FAULT.
        const auto segs = segments(run);
        CHECK(segs.size() > 3 && segs[0].mode == Mode::Charge && segs[0].start_s == 0.0 && segs[1].mode == Mode::Thrust &&
              segs[1].start_s == sc.step_s);
        CHECK_EQ(run.seconds_in(Mode::Safe), 0.0);
        CHECK_EQ(run.seconds_in(Mode::Fault), 0.0);
        CHECK(run.seconds_in(Mode::Coast) > 0.0);
        CHECK(lowest_wh >= 0.2 * sat.battery_capacity_wh);

        const double beta = abep::beta_angle(sc.initial, sc.sun_unit);
        const double r_mean = 0.5 * (abep::norm(sc.initial.r) + abep::norm(run.final_state.r));
        const double eclipse = abep::circular_orbit_eclipse_fraction(r_mean, beta);
        CHECK_NEAR(books_eclipse / run.elapsed_s, eclipse, 0.002);
        const double hours = run.elapsed_s / 3600.0;
        const double closed = abep::sustainable_thrust_duty(sat.array_power_w, 1.0 - books_eclipse / run.elapsed_s, sat.bus_load_w,
                                                            sat.thruster_power_w);
        const double bound = sat.battery_capacity_wh / (sat.thruster_power_w * hours);
        CHECK_NEAR(run.thrust_duty(), closed, bound);
        std::printf("    thrust duty over 40 orbits %.4f, closed form %.4f, bound %.4f (eclipse %.4f of an orbit, lowest battery %.0f Wh)\n",
                    run.thrust_duty(), closed, bound, eclipse, lowest_wh);
    }
    {
        TEST("mission: 100 orbits, thrusting in eclipse too: the duty matches the power budget");
        // Tolerance fixed before running: capacity / (thruster power x time) of the closed form evaluated with the run's own
        // sunlit share, the same bound as above (2500 Wh is 0.84% of the thruster energy at full power over 100 orbits,
        // 2000 W for 149 h). Array 3000 W, load 500 W, thruster 2000 W: the closed-form duty is 0.65, above the sunlit
        // share of 0.60, so the thruster has to run in the shadow too.
        abep::Satellite sat = example_satellite();
        sat.array_power_w = 3000.0;
        sat.battery_capacity_wh = 2500.0;
        sat.battery_initial_wh = 1500.0;
        abep::Scenario sc = example_scenario(250.0e3, 90.0, 30.0, 0.0);
        const double period = abep::orbital_period_two_body(abep::norm(sc.initial.r));
        sc.duration_s = std::floor(100.0 * period / sc.step_s) * sc.step_s;
        abep::ControllerConfig cfg;
        cfg.thrust_in_eclipse = true;
        const auto run = abep::run_mission(sat, sc, cfg, atmosphere());
        CHECK(run.end_reason == abep::EndReason::Completed);
        CHECK_EQ(run.seconds_in(Mode::Fault), 0.0);
        CHECK_EQ(run.seconds_in(Mode::Safe), 0.0);  // never driven to SAFE
        double spilled = 0.0;
        double eclipse_s = 0.0;
        for (const abep::OrbitBalance& o : run.orbits) {
            spilled += o.spilled_wh;
            eclipse_s += o.eclipse_s;
        }
        CHECK_NEAR(spilled, 0.0, 1.0e-6);
        const double closed = abep::sustainable_thrust_duty(sat.array_power_w, 1.0 - eclipse_s / run.elapsed_s, sat.bus_load_w,
                                                            sat.thruster_power_w);
        const double bound = sat.battery_capacity_wh / (sat.thruster_power_w * run.elapsed_s / 3600.0);
        CHECK_NEAR(run.thrust_duty(), closed, bound);
        std::printf("    thrust duty over 100 orbits %.4f, closed form %.4f, bound %.4f\n", run.thrust_duty(), closed, bound);
    }
    {
        TEST("mission: a thruster that heats to its limit cycles at the duty the thermal equations give");
        // The thruster draws 1000 W at an implied efficiency of 13.7%, so 863 W of waste heat goes into a node with
        // C = 5000 J/K and G = 5 W/K (time constant 1000 s, sink 300 K). It stops at 400 K and may restart at 360 K.
        // Heating from 360 K to 400 K takes tau ln((T_inf - 360) / (T_inf - 400)) and cooling back takes
        // tau ln((400 - 300) / (360 - 300)), with T_inf = 300 + 863 / 5 = 472.6 K.
        // Tolerances fixed before running, over 200,000 s with the first 3,000 s (the first heat-up from 300 K) left out:
        //  - 3% of duty against those closed forms. The machine switches only at step boundaries and the temperature
        //    overshoots each threshold by up to one step's change (0.7 K heating, 0.6 K cooling, each about one step of
        //    time), so each phase is off by up to a few steps of 10 s on a cycle of 950 s.
        //  - 0.5% against the same rule run on its own at the same step (no orbit, no power): the two differ only in where
        //    the window cuts the last cycle, at most one cycle in 210, 0.2% of duty.
        abep::Satellite sat = example_satellite();
        sat.thruster_power_w = 1000.0;
        sat.array_power_w = 20000.0;
        sat.battery_capacity_wh = 1.0e5;
        sat.battery_initial_wh = 9.0e4;
        const abep::ThrusterThermal thermal{5000.0, 5.0, 300.0};
        sat.thermal = thermal;
        abep::ControllerConfig cfg;
        cfg.thrust_in_eclipse = true;
        cfg.temperature_resume_k = 360.0;
        cfg.temperature_limit_k = 400.0;
        cfg.temperature_fault_k = 450.0;
        abep::Scenario sc = example_scenario(250.0e3, 90.0, 30.0, 200000.0);
        const auto run = abep::run_mission(sat, sc, cfg, atmosphere());
        CHECK(run.end_reason == abep::EndReason::Completed);

        const double waste_w = sat.thruster_power_w * (1.0 - abep::implied_thruster_efficiency(sat.thruster));
        const double t_inf = abep::steady_state_temperature(thermal, waste_w);
        const double tau = thermal.heat_capacity_j_per_k / thermal.conductance_w_per_k;
        const double heat_s = tau * std::log((t_inf - 360.0) / (t_inf - 400.0));
        const double cool_s = tau * std::log((400.0 - 300.0) / (360.0 - 300.0));
        const double closed = heat_s / (heat_s + cool_s);

        double max_t = 0.0;
        double min_t_after_warmup = 1.0e9;
        long thrust_samples = 0;
        long counted = 0;
        for (const abep::Sample& s : run.samples) {
            if (s.t_s < 3000.0) continue;
            ++counted;
            if (s.mode == Mode::Thrust) ++thrust_samples;
            max_t = std::max(max_t, s.temperature_k);
            min_t_after_warmup = std::min(min_t_after_warmup, s.temperature_k);
        }
        const double duty = static_cast<double>(thrust_samples) / static_cast<double>(counted);

        // The same rule on its own: start-of-step temperature decides, then the heat for the step is applied.
        double temperature = 300.0;
        bool on = false;
        long ref_thrust = 0;
        long ref_counted = 0;
        for (long i = 0; i < 20000; ++i) {
            if (on) {
                on = !(temperature >= 400.0);
            } else {
                on = temperature <= 360.0;
            }
            if (static_cast<double>(i) * sc.step_s >= 3000.0) {
                ++ref_counted;
                if (on) ++ref_thrust;
            }
            temperature = abep::thermal_step(thermal, temperature, on ? waste_w : 0.0, sc.step_s);
        }
        const double ref_duty = static_cast<double>(ref_thrust) / static_cast<double>(ref_counted);

        CHECK_NEAR(duty, closed, 0.03);
        CHECK_NEAR(duty, ref_duty, 0.005);
        // A step that starts below the limit ends no more than one step's heating above it, and a step that starts above
        // the restart temperature ends no more than one step's cooling below it; the samples are start-of-step values.
        const double decay_per_step = 1.0 - std::exp(-sc.step_s / tau);
        CHECK(max_t <= 400.0 + (t_inf - 400.0) * decay_per_step + 1.0e-9);
        CHECK(min_t_after_warmup >= 360.0 - (360.0 - 300.0) * decay_per_step - 1.0e-9);
        CHECK_EQ(run.seconds_in(Mode::Fault), 0.0);
        CHECK(run.seconds_in(Mode::Coast) > 0.0);
        std::printf("    thermal cycle: duty %.4f, closed form %.4f, rule alone %.4f; temperature %.1f to %.1f K\n", duty, closed, ref_duty,
                    min_t_after_warmup, max_t);
    }
    {
        TEST("mission: an array outage runs the battery down to SAFE, and the satellite works its way back up");
        // A polar orbit with beta = 80 deg never enters the shadow at 250 km (the shadow needs |beta| < 74.3 deg), so the only
        // power loss is the outage, 1000 s to 4000 s. The battery (400 Wh) carries the 500 W bus load: it passes 20%
        // (CHARGE) at about 3310 s and 10% (SAFE) at about 3600 s, where the load drops to 100 W; the array returns at
        // 4000 s, SAFE ends at 30% (about 4140 s), CHARGE at 50% (about 4290 s). Tolerance fixed before running: 100 s on
        // those times, the sequence of modes exact. The satellite starts in SAFE and is moved up to CHARGE by the very
        // first step, so the first sample is CHARGE.
        abep::Satellite sat = example_satellite();
        sat.battery_capacity_wh = 400.0;
        sat.battery_initial_wh = 400.0;
        abep::Scenario sc = example_scenario(250.0e3, 90.0, 80.0, 6000.0);
        sc.array_outages = {{1000.0, 4000.0}};
        const auto run = abep::run_mission(sat, sc, abep::ControllerConfig{}, atmosphere());
        const auto segs = segments(run);
        CHECK(sequence_is(segs, {Mode::Charge, Mode::Thrust, Mode::Coast, Mode::Charge, Mode::Safe, Mode::Charge, Mode::Thrust}));
        if (segs.size() == 7) {
            CHECK_EQ(segs[2].start_s, 1000.0);  // thrusting stops the step the array does
            CHECK_NEAR(segs[3].start_s, 3310.0, 100.0);
            CHECK_NEAR(segs[4].start_s, 3600.0, 100.0);
            CHECK(segs[5].start_s > 4000.0);  // SAFE lasts until the array is back
            CHECK_NEAR(segs[5].start_s, 4140.0, 100.0);
            CHECK_NEAR(segs[6].start_s, 4290.0, 100.0);
        }
        double lowest = sat.battery_capacity_wh;
        long thrust_off_thrust = 0;
        long load_wrong = 0;
        for (const abep::Sample& s : run.samples) {
            lowest = std::min(lowest, s.battery_wh);
            if (s.mode != Mode::Thrust && s.thrust_n != 0.0) ++thrust_off_thrust;
            if (s.mode == Mode::Safe && s.load_w != sat.safe_load_w) ++load_wrong;
            if (s.mode != Mode::Safe && s.load_w != sat.bus_load_w) ++load_wrong;
        }
        CHECK(lowest > 0.0);  // SAFE saved it from running dry
        CHECK_EQ(thrust_off_thrust, 0L);
        CHECK_EQ(load_wrong, 0L);
        CHECK_EQ(run.seconds_in(Mode::Fault), 0.0);
        double spilled = 0.0;
        for (const abep::OrbitBalance& o : run.orbits) spilled += std::min(0.0, o.spilled_wh);  // shortfalls only
        CHECK_NEAR(spilled, 0.0, 1.0e-6);
        std::printf("    lowest battery %.1f Wh of 400; SAFE from %.0f s to %.0f s\n", lowest, start_of(segs, 4), start_of(segs, 5));
    }
    {
        TEST("mission: a fault window puts the satellite in FAULT, holds, and recovers through SAFE and CHARGE");
        // A fault is asserted from 2000 s to 2300 s with a hold of 300 s. The count of clear time starts at the step at
        // 2300 s, so the 30th clear step (the one at 2590 s) reaches 300 s: SAFE at 2590 s, CHARGE at 2600 s, THRUST at
        // 2610 s. Exact, in 10 s steps. (The start is CHARGE at 0 s and THRUST at 10 s, as in the other runs.)
        abep::Satellite sat = example_satellite();
        sat.battery_capacity_wh = 400.0;
        sat.battery_initial_wh = 400.0;
        abep::Scenario sc = example_scenario(250.0e3, 90.0, 80.0, 4000.0);
        sc.fault_windows = {{2000.0, 2300.0}};
        abep::ControllerConfig cfg;
        cfg.fault_hold_s = 300.0;
        const auto run = abep::run_mission(sat, sc, cfg, atmosphere());
        const auto segs = segments(run);
        CHECK(sequence_is(segs, {Mode::Charge, Mode::Thrust, Mode::Fault, Mode::Safe, Mode::Charge, Mode::Thrust}));
        if (segs.size() == 6) {
            CHECK_EQ(segs[2].start_s, 2000.0);
            CHECK_EQ(segs[3].start_s, 2590.0);
            CHECK_EQ(segs[4].start_s, 2600.0);
            CHECK_EQ(segs[5].start_s, 2610.0);
        }
        long thrust_off_thrust = 0;
        long load_wrong = 0;
        for (const abep::Sample& s : run.samples) {
            if (s.mode != Mode::Thrust && s.thrust_n != 0.0) ++thrust_off_thrust;
            const bool shed = s.mode == Mode::Safe || s.mode == Mode::Fault;
            if (s.load_w != (shed ? sat.safe_load_w : sat.bus_load_w)) ++load_wrong;
        }
        CHECK_EQ(thrust_off_thrust, 0L);
        CHECK_EQ(load_wrong, 0L);
        CHECK_EQ(run.seconds_in(Mode::Fault), 590.0);  // 2000 s to 2590 s
    }
    {
        TEST("mission: running out of energy is a fault, and recovery waits for the array");
        // No array for 3000 s from the start with a small battery, and a bus load that cannot be carried: the battery
        // empties, the shortfall is flagged, and the satellite is in FAULT until the array returns and the hold passes.
        abep::Satellite sat = example_satellite();
        sat.battery_capacity_wh = 40.0;
        sat.battery_initial_wh = 40.0;
        sat.safe_load_w = 300.0;  // even SAFE cannot be carried
        abep::Scenario sc = example_scenario(250.0e3, 90.0, 80.0, 5000.0);
        sc.array_outages = {{500.0, 3000.0}};
        abep::ControllerConfig cfg;
        cfg.fault_hold_s = 200.0;
        const auto run = abep::run_mission(sat, sc, cfg, atmosphere());
        CHECK(run.seconds_in(Mode::Fault) > 0.0);
        double shortfall = 0.0;
        for (const abep::OrbitBalance& o : run.orbits) shortfall += std::min(0.0, o.spilled_wh);
        CHECK(shortfall < 0.0);  // some demand really went unmet
        CHECK(run.final_battery_wh >= 0.0 && run.final_battery_wh <= sat.battery_capacity_wh);
        const auto segs = segments(run);
        CHECK(segs.back().mode == Mode::Thrust);  // and it is back at work by the end
    }
    {
        TEST("mission: the thruster is offered only what the battery can give in a step, so it never empties it alone");
        // A 10 Wh battery, no bus load and no array: 10 s of the 2000 W thruster is 5.56 Wh, so the second step of thrusting
        // (at 4.44 Wh) can be offered only the 1600 W the battery holds. Taking all 2000 W would ask for 1.1 Wh the battery
        // does not have, which the run would report as a shortfall and a fault. Exact: no shortfall, no fault.
        abep::Satellite sat = example_satellite();
        sat.battery_capacity_wh = 10.0;
        sat.battery_initial_wh = 10.0;
        sat.bus_load_w = 0.0;
        sat.safe_load_w = 0.0;
        abep::Scenario sc = example_scenario(250.0e3, 90.0, 80.0, 200.0);
        sc.array_outages = {{0.0, 200.0}};
        abep::ControllerConfig cfg;
        cfg.thrust_in_eclipse = true;
        const auto run = abep::run_mission(sat, sc, cfg, atmosphere());
        double shortfall = 0.0;
        for (const abep::OrbitBalance& o : run.orbits) shortfall += std::min(0.0, o.spilled_wh);
        CHECK_NEAR(shortfall, 0.0, 1.0e-6);
        CHECK_EQ(run.seconds_in(Mode::Fault), 0.0);
        CHECK(run.samples.size() > 3 && run.samples[1].mode == Mode::Thrust && run.samples[2].mode == Mode::Thrust);
        if (run.samples.size() > 3) {
            CHECK_NEAR(run.samples[1].thruster_power_w, 2000.0, 1.0e-9);  // the first thrusting step has 10 Wh to draw on
            CHECK_NEAR(run.samples[2].thruster_power_w, 1600.0, 1.0e-6);  // the second is offered what is left: 4.44 Wh over 10 s
        }
        CHECK(run.final_battery_wh >= 0.0);
    }
    {
        TEST("mission: the density flag keeps thrusting to the denser part of the orbit");
        // A circular orbit at 51.6 deg inclination does not keep one altitude above the ellipsoid: the ellipsoid itself puts
        // it 13 km higher at the highest latitude than at the equator, and J2 makes the radius oscillate as well; the
        // density follows. So first run with no density limit to see the range the orbit really goes through (288 to
        // 306 km, in the first attempt at this test), then start thrusting at 60% and stop below 40% of that range in log
        // density, with power to spare so that only the density matters. Checked from the samples: every sample in THRUST is
        // at or above the stop density, every start is at or above the start density, and both THRUST and density-blocked
        // COAST occur. Exact.
        abep::Satellite sat = example_satellite();
        sat.array_power_w = 10000.0;
        sat.battery_capacity_wh = 1.0e5;
        sat.battery_initial_wh = 9.0e4;
        abep::Scenario sc = example_scenario(300.0e3, 51.6, 0.0, 55000.0);
        abep::ControllerConfig cfg;
        cfg.thrust_in_eclipse = true;
        const auto baseline = abep::run_mission(sat, sc, cfg, atmosphere());
        double lowest = 1.0;
        double highest = 0.0;
        for (const abep::Sample& s : baseline.samples) {
            lowest = std::min(lowest, s.density_kg_m3);
            highest = std::max(highest, s.density_kg_m3);
        }
        const double span = std::log(highest / lowest);
        cfg.min_density_stop_kg_m3 = lowest * std::exp(0.4 * span);
        cfg.min_density_start_kg_m3 = lowest * std::exp(0.6 * span);
        const auto run = abep::run_mission(sat, sc, cfg, atmosphere());
        CHECK(run.end_reason == abep::EndReason::Completed);
        long below_stop_in_thrust = 0;
        long started_below_start = 0;
        long thrust_samples = 0;
        long coast_in_thin_air = 0;
        double min_density = 1.0;
        double max_density = 0.0;
        for (std::size_t i = 0; i < run.samples.size(); ++i) {
            const abep::Sample& s = run.samples[i];
            min_density = std::min(min_density, s.density_kg_m3);
            max_density = std::max(max_density, s.density_kg_m3);
            if (s.mode == Mode::Thrust) {
                ++thrust_samples;
                if (s.density_kg_m3 < cfg.min_density_stop_kg_m3) ++below_stop_in_thrust;
                if (i > 0 && run.samples[i - 1].mode != Mode::Thrust && s.density_kg_m3 < cfg.min_density_start_kg_m3) ++started_below_start;
            }
            if (s.mode == Mode::Coast && s.density_kg_m3 < cfg.min_density_start_kg_m3) ++coast_in_thin_air;
        }
        CHECK(min_density < cfg.min_density_stop_kg_m3);   // the orbit really does cross both thresholds
        CHECK(max_density > cfg.min_density_start_kg_m3);
        CHECK(thrust_samples > 0);
        CHECK(coast_in_thin_air > 0);
        CHECK_EQ(below_stop_in_thrust, 0L);
        CHECK_EQ(started_below_start, 0L);
        CHECK(run.thrust_duty() > 0.05 && run.thrust_duty() < 0.95);
        std::printf("    density %.2e to %.2e kg/m^3 around the orbit; thrust duty %.3f\n", min_density, max_density, run.thrust_duty());
    }
    {
        TEST("mission: with power to spare the thruster holds altitude where drag alone would not");
        // 220 km, no J2 (so the specific energy changes only by the work of drag and thrust), a 500 kg satellite, thrust
        // at full power (27 mN) against about 9 mN of drag, for 10 orbits. Bounds fixed before running, from the estimates
        // thrust-minus-drag +1.5 kJ/kg per orbit and drag alone -0.7 kJ/kg per orbit: with the thruster the orbit gains more
        // than 5 kJ/kg, without it the orbit loses more than 3 kJ/kg.
        abep::Satellite sat = example_satellite();
        sat.mass_kg = 500.0;
        sat.array_power_w = 3000.0;
        abep::Scenario sc = example_scenario(220.0e3, 90.0, 80.0, 53000.0);
        sc.include_j2 = false;
        const double e0 = abep::specific_energy(sc.initial);
        const auto with_thruster = abep::run_mission(sat, sc, abep::ControllerConfig{}, atmosphere());
        sc.thruster_enabled = false;
        const auto without = abep::run_mission(sat, sc, abep::ControllerConfig{}, atmosphere());
        const double gain = abep::specific_energy(with_thruster.final_state) - e0;
        const double loss = e0 - abep::specific_energy(without.final_state);
        CHECK(gain > 5.0e3);
        CHECK(loss > 3.0e3);
        CHECK(with_thruster.thrust_duty() > 0.9);  // sunlight all the time and power to spare
        std::printf("    energy change over 10 orbits: %+.1f kJ/kg with the thruster, %+.1f kJ/kg without\n", 1.0e-3 * gain, -1.0e-3 * loss);
    }
    {
        TEST("mission: the same inputs give the same run");
        const abep::Satellite sat = example_satellite();
        abep::Scenario sc = example_scenario(250.0e3, 90.0, 30.0, 30000.0);
        sc.array_outages = {{5000.0, 9000.0}};
        const auto a = abep::run_mission(sat, sc, abep::ControllerConfig{}, atmosphere());
        const auto b = abep::run_mission(sat, sc, abep::ControllerConfig{}, atmosphere());
        bool same = a.samples.size() == b.samples.size() && a.final_battery_wh == b.final_battery_wh &&
                    a.final_state.r.x == b.final_state.r.x && a.final_state.r.y == b.final_state.r.y &&
                    a.final_state.r.z == b.final_state.r.z && a.final_state.v.x == b.final_state.v.x;
        for (std::size_t i = 0; same && i < a.samples.size(); ++i) {
            const abep::Sample& x = a.samples[i];
            const abep::Sample& y = b.samples[i];
            same = x.mode == y.mode && x.altitude_m == y.altitude_m && x.thrust_n == y.thrust_n && x.battery_wh == y.battery_wh &&
                   x.drag_n == y.drag_n && x.temperature_k == y.temperature_k;
        }
        CHECK(same);
    }
    {
        TEST("mission: keeping one sample in ten leaves the run itself unchanged");
        const abep::Satellite sat = example_satellite();
        abep::Scenario sc = example_scenario(250.0e3, 90.0, 30.0, 20000.0);
        const auto full = abep::run_mission(sat, sc, abep::ControllerConfig{}, atmosphere());
        sc.sample_every = 10;
        const auto thin = abep::run_mission(sat, sc, abep::ControllerConfig{}, atmosphere());
        CHECK_EQ(thin.samples.size(), full.samples.size() / 10);
        CHECK(thin.final_state.r.x == full.final_state.r.x && thin.final_state.v.z == full.final_state.v.z);
        CHECK_EQ(thin.final_battery_wh, full.final_battery_wh);
        bool aligned = true;
        for (std::size_t i = 0; i < thin.samples.size(); ++i) aligned = aligned && thin.samples[i].t_s == full.samples[10 * i].t_s;
        CHECK(aligned);
    }
}
