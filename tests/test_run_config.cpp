#include "check.hpp"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>

#include "abep/constants.hpp"
#include "abep/flight.hpp"
#include "abep/mission.hpp"
#include "abep/orbit.hpp"
#include "abep/run_config.hpp"
#include "abep/standard_atmosphere_1976.hpp"
#include "abep/units.hpp"

namespace {

namespace units = abep::units;

const abep::DensityTable& ussa() {
    static const abep::DensityTable table = abep::ussa1976::density_table();
    return table;
}

}  // namespace

void runRunConfigTests() {
    {
        TEST("settings: every setting is in the table once, with a name, and the table covers the whole struct");
        const auto& table = abep::setting_table();
        // RunSettings is nothing but doubles, so a setting left out of the table shows as a size that does not add up.
        CHECK_EQ(sizeof(abep::RunSettings), table.size() * sizeof(double));
        std::set<std::string> names;
        std::set<const void*> fields;
        const abep::RunSettings base;
        for (const abep::SettingInfo& s : table) {
            names.insert(s.key);
            fields.insert(reinterpret_cast<const void*>(&(base.*(s.field))));
            CHECK(std::string(s.key).size() > 2);
            CHECK(std::string(s.meaning).size() > 5);
        }
        CHECK_EQ(names.size(), table.size());
        CHECK_EQ(fields.size(), table.size());
        // Setting a setting to the value it already has changes nothing, and every one of them reaches its own field.
        for (const abep::SettingInfo& s : table) {
            abep::RunSettings a;
            abep::set_setting(a, s.key, a.*(s.field) + 0.0);
            CHECK_EQ(a.*(s.field), base.*(s.field));
            abep::RunSettings b;
            abep::set_setting(b, s.key, 0.25);
            CHECK_EQ(b.*(s.field), 0.25);
        }
    }
    {
        TEST("settings: a name that is not a setting, and a value that is not a number, are refused");
        abep::RunSettings s;
        CHECK_THROWS(abep::set_setting(s, "altitude", 300.0), std::invalid_argument);
        CHECK_THROWS(abep::set_setting(s, "", 300.0), std::invalid_argument);
        CHECK_THROWS(abep::set_setting(s, "Altitude_km", 300.0), std::invalid_argument);  // names are case sensitive
        CHECK_THROWS(abep::set_setting(s, "altitude_km", std::numeric_limits<double>::quiet_NaN()), std::invalid_argument);
        CHECK_THROWS(abep::set_setting(s, "altitude_km", std::numeric_limits<double>::infinity()), std::invalid_argument);
        CHECK_EQ(s.altitude_km, 250.0);  // none of those changed it
        try {
            abep::set_setting(s, "altitud_km", 300.0);
        } catch (const std::invalid_argument& e) {
            CHECK(std::string(e.what()).find("altitud_km") != std::string::npos);  // the message names the setting
        }
    }
    {
        TEST("settings: the defaults are a valid mission, the thresholds are the state machine's own, and it runs and thrusts");
        const abep::RunSettings d;
        const abep::ControllerConfig c;
        CHECK_EQ(d.soc_safe_enter, c.soc_safe_enter);
        CHECK_EQ(d.soc_safe_exit, c.soc_safe_exit);
        CHECK_EQ(d.soc_thrust_stop, c.soc_thrust_stop);
        CHECK_EQ(d.soc_thrust_start, c.soc_thrust_start);
        CHECK_EQ(d.fault_hold_s, c.fault_hold_s);
        CHECK_EQ(d.thrust_in_eclipse != 0.0, c.thrust_in_eclipse);

        abep::RunSettings s;
        s.duration_days = 0.25;
        const abep::Mission m = abep::make_mission(s);
        const abep::MissionResult r = abep::run_mission(m.satellite, m.scenario, m.controller, ussa());
        CHECK(r.end_reason == abep::EndReason::Completed);
        CHECK(!r.samples.empty());
        CHECK(r.seconds_in(abep::Mode::Thrust) > 0.0);
        CHECK(r.thrust_duty() > 0.1 && r.thrust_duty() < 1.0);  // it thrusts, and not in eclipse
        std::printf("    the default mission, a quarter of a day: thrust duty %.3f over %zu orbits\n", r.thrust_duty(), r.orbits.size());
    }
    {
        TEST("settings: each one reaches the model, in the unit its name gives");
        abep::RunSettings s;
        s.altitude_km = 400.0;
        s.inclination_deg = 98.0;
        s.raan_deg = 45.0;
        s.mass_kg = 250.0;
        s.frontal_area_m2 = 1.5;
        s.drag_coefficient = 2.1;
        s.thrust_density_mn_per_kw = 12.0;
        s.exhaust_velocity_km_s = 25.0;
        s.intake_area_m2 = 3.0;
        s.capture_efficiency = 0.5;
        s.thruster_power_w = 900.0;
        s.array_power_w = 2000.0;
        s.bus_load_w = 400.0;
        s.safe_load_w = 80.0;
        s.battery_capacity_wh = 1200.0;
        s.battery_initial_wh = 1000.0;
        s.duration_days = 2.0;
        s.step_s = 5.0;
        s.sample_every = 12.0;
        s.include_j2 = 0.0;
        s.corotating_atmosphere = 0.0;
        s.thruster_enabled = 0.0;
        s.thrust_in_eclipse = 1.0;
        s.soc_thrust_start = 0.6;
        s.fault_hold_s = 300.0;
        const abep::Mission m = abep::make_mission(s);
        const abep::Satellite& sat = m.satellite;
        CHECK_EQ(sat.mass_kg, 250.0);
        CHECK_EQ(sat.frontal_area_m2, 1.5);
        CHECK_EQ(sat.drag_coefficient, 2.1);
        CHECK_EQ(sat.thruster.thrust_density_n_per_w, units::mn_per_kw_to_n_per_w(12.0));
        CHECK(std::fabs(sat.thruster.thrust_density_n_per_w - 12.0e-6) <= 1.0e-18);
        CHECK_EQ(sat.thruster.exhaust_velocity_m_s, 25000.0);
        CHECK_EQ(sat.thruster.intake_area_m2, 3.0);
        CHECK_EQ(sat.thruster.capture_efficiency, 0.5);
        CHECK_EQ(sat.thruster_power_w, 900.0);
        CHECK_EQ(sat.array_power_w, 2000.0);
        CHECK_EQ(sat.bus_load_w, 400.0);
        CHECK_EQ(sat.safe_load_w, 80.0);
        CHECK_EQ(sat.battery_capacity_wh, 1200.0);
        CHECK_EQ(sat.battery_initial_wh, 1000.0);
        CHECK(!sat.thermal.has_value());
        const abep::Scenario& sc = m.scenario;
        CHECK(std::fabs(abep::norm(sc.initial.r) - (abep::constants::kEarthEquatorialRadius + 400.0e3)) <= 1.0e-6);
        CHECK(std::fabs(abep::inclination(sc.initial) - units::deg_to_rad(98.0)) <= 1.0e-12);
        CHECK(std::fabs(abep::raan(sc.initial) - units::deg_to_rad(45.0)) <= 1.0e-12);
        CHECK_EQ(sc.duration_s, 2.0 * 86400.0);
        CHECK_EQ(sc.step_s, 5.0);
        CHECK_EQ(sc.sample_every, std::size_t(12));
        CHECK(!sc.include_j2);
        CHECK(!sc.corotating_atmosphere);
        CHECK(!sc.thruster_enabled);
        CHECK_EQ(sc.sun_unit.x, 1.0);
        const abep::ControllerConfig& c = m.controller;
        CHECK(c.thrust_in_eclipse);
        CHECK_EQ(c.soc_thrust_start, 0.6);
        CHECK_EQ(c.fault_hold_s, 300.0);
        CHECK(std::isinf(c.temperature_limit_k));  // 0 in the settings means no limit
    }
    {
        TEST("settings: each on/off setting changes its own flag and no other");
        // Changed one at a time from the defaults, so that two settings that happen to carry the same value cannot be mixed up.
        const auto flags_of = [](const abep::RunSettings& s) {
            const abep::Mission m = abep::make_mission(s);
            return std::string() + (m.scenario.include_j2 ? "J" : "-") + (m.scenario.corotating_atmosphere ? "R" : "-") +
                   (m.scenario.thruster_enabled ? "T" : "-") + (m.controller.thrust_in_eclipse ? "E" : "-");
        };
        CHECK(flags_of(abep::RunSettings{}) == "JRT-");
        abep::RunSettings j;
        j.include_j2 = 0.0;
        CHECK(flags_of(j) == "-RT-");
        abep::RunSettings r;
        r.corotating_atmosphere = 0.0;
        CHECK(flags_of(r) == "J-T-");
        abep::RunSettings t;
        t.thruster_enabled = 0.0;
        CHECK(flags_of(t) == "JR--");
        abep::RunSettings e;
        e.thrust_in_eclipse = 1.0;
        CHECK(flags_of(e) == "JRTE");
    }
    {
        TEST("settings: the thermal node and its limits are there only when asked for");
        abep::RunSettings s;
        CHECK(!abep::make_mission(s).satellite.thermal.has_value());
        s.thermal_heat_capacity_j_per_k = 5000.0;
        s.thermal_conductance_w_per_k = 5.0;
        s.thermal_sink_k = 290.0;
        s.temperature_resume_k = 360.0;
        s.temperature_limit_k = 400.0;
        s.temperature_fault_k = 450.0;
        const abep::Mission m = abep::make_mission(s);
        CHECK(m.satellite.thermal.has_value());
        if (m.satellite.thermal) {
            CHECK_EQ(m.satellite.thermal->heat_capacity_j_per_k, 5000.0);
            CHECK_EQ(m.satellite.thermal->conductance_w_per_k, 5.0);
            CHECK_EQ(m.satellite.thermal->sink_temperature_k, 290.0);
        }
        CHECK_EQ(m.satellite.initial_temperature_k, 290.0);  // it starts at the sink temperature
        CHECK_EQ(m.controller.temperature_resume_k, 360.0);
        CHECK_EQ(m.controller.temperature_limit_k, 400.0);
        CHECK_EQ(m.controller.temperature_fault_k, 450.0);
    }
    {
        TEST("settings: values that make no sense are refused, and the message names the setting where it can");
        const auto refuses = [](const char* key, double value) {
            abep::RunSettings s;
            abep::set_setting(s, key, value);
            try {
                (void)abep::make_mission(s);
            } catch (const std::invalid_argument&) {
                return true;
            }
            return false;
        };
        CHECK(refuses("mass_kg", 0.0));
        CHECK(refuses("mass_kg", -5.0));
        CHECK(refuses("frontal_area_m2", 0.0));
        CHECK(refuses("capture_efficiency", 1.5));
        CHECK(refuses("thrust_density_mn_per_kw", 120.0));  // with 20 km/s that is 1/2 * 120e-6 * 20e3 = 120%: more than a thruster can be
        CHECK(!refuses("thrust_density_mn_per_kw", 100.0)); // and 100 mN/kW with 20 km/s is exactly 100%, which is allowed
        CHECK(refuses("battery_initial_wh", 5000.0));       // more than the capacity
        CHECK(refuses("step_s", 0.0));
        CHECK(refuses("duration_days", -1.0));
        CHECK(refuses("altitude_km", -7000.0));              // inside the Earth
        CHECK(refuses("soc_thrust_start", 0.1));             // below the stop level: no hysteresis
        CHECK(refuses("sample_every", 0.0));
        CHECK(refuses("sample_every", 2.5));
        CHECK(refuses("sample_every", 2.0e9));
        CHECK(refuses("include_j2", 2.0));
        CHECK(refuses("thruster_enabled", -1.0));
        CHECK(refuses("thrust_in_eclipse", 0.5));
        CHECK(!refuses("sample_every", 1.0));
        CHECK(!refuses("include_j2", 0.0));
        abep::RunSettings s;
        s.include_j2 = 2.0;
        try {
            (void)abep::make_mission(s);
        } catch (const std::invalid_argument& e) {
            CHECK(std::string(e.what()).find("include_j2") != std::string::npos);
        }
    }
}
