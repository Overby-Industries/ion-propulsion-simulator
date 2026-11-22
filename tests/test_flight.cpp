#include "check.hpp"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>

#include "abep/flight.hpp"

namespace {

using abep::ControllerConfig;
using abep::Flags;
using abep::FlightComputer;
using abep::Mode;

// A configuration with every flag in play. The numbers are round so the expectations can be read off; they are an
// operating policy, not data about any satellite. State of charge keeps the defaults: safe at 0.10 and 0.30, thrust
// stops below 0.20 and starts at 0.50.
ControllerConfig example_config() {
    ControllerConfig c;
    c.min_density_start_kg_m3 = 2.0e-11;
    c.min_density_stop_kg_m3 = 1.0e-11;
    c.temperature_resume_k = 350.0;
    c.temperature_limit_k = 400.0;
    c.temperature_fault_k = 450.0;
    c.fault_hold_s = 600.0;
    return c;
}

// Flags in which nothing holds the thruster back: charged, sunlit, dense air, cool.
Flags ready() {
    Flags f;
    f.battery_fraction = 0.8;
    f.sunlit = true;
    f.density_kg_m3 = 5.0e-11;
    f.temperature_k = 300.0;
    return f;
}

// The mode after one update of 10 s, starting from `from`.
Mode next_from(Mode from, const Flags& f, const ControllerConfig& c = example_config()) {
    FlightComputer computer(c, from);
    return computer.update(f, 10.0);
}

// The rules of docs/FLIGHT_MODES.md, written out a second time in a different form, for one update of 10 s (shorter
// than the fault hold, so FAULT never hands over in this one step).
Mode reference_next(Mode from, const Flags& f, const ControllerConfig& c) {
    if (f.fault || f.temperature_k >= c.temperature_fault_k) return Mode::Fault;
    if (from == Mode::Fault) return Mode::Fault;
    const double soc = f.battery_fraction;
    if (from == Mode::Safe) return soc >= c.soc_safe_exit ? Mode::Charge : Mode::Safe;
    if (soc < c.soc_safe_enter) return Mode::Safe;
    const bool has_power = f.sunlit || c.thrust_in_eclipse;
    const bool can_start = f.thruster_available && has_power && soc >= c.soc_thrust_start &&
                           f.density_kg_m3 >= c.min_density_start_kg_m3 && f.temperature_k <= c.temperature_resume_k;
    const bool can_continue = f.thruster_available && has_power && f.density_kg_m3 >= c.min_density_stop_kg_m3 &&
                              f.temperature_k < c.temperature_limit_k;
    switch (from) {
        case Mode::Charge:
            if (soc < c.soc_thrust_start) return Mode::Charge;
            return can_start ? Mode::Thrust : Mode::Coast;
        case Mode::Coast:
            if (soc < c.soc_thrust_stop) return Mode::Charge;
            return can_start ? Mode::Thrust : Mode::Coast;
        case Mode::Thrust:
            if (soc < c.soc_thrust_stop) return Mode::Charge;
            return can_continue ? Mode::Thrust : Mode::Coast;
        default:
            return from;
    }
}

}  // namespace

void runFlightTests() {
    {
        TEST("flight: the modes have their names");
        CHECK(std::strcmp(abep::mode_name(Mode::Safe), "SAFE") == 0);
        CHECK(std::strcmp(abep::mode_name(Mode::Charge), "CHARGE") == 0);
        CHECK(std::strcmp(abep::mode_name(Mode::Coast), "COAST") == 0);
        CHECK(std::strcmp(abep::mode_name(Mode::Thrust), "THRUST") == 0);
        CHECK(std::strcmp(abep::mode_name(Mode::Fault), "FAULT") == 0);
        CHECK_EQ(abep::kModeCount, static_cast<std::size_t>(5));
        const FlightComputer defaults;  // the default configuration is a valid one, and a satellite starts in SAFE
        CHECK(defaults.mode() == Mode::Safe);
    }
    {
        TEST("flight: configurations and flags that make no sense are refused");
        const double nan = std::numeric_limits<double>::quiet_NaN();
        {
            ControllerConfig c;
            c.soc_safe_enter = 0.4;  // not below safe_exit (0.30)
            CHECK_THROWS(FlightComputer{c}, std::invalid_argument);
        }
        {
            ControllerConfig c;
            c.soc_thrust_stop = 0.5;  // not below thrust_start (0.50)
            CHECK_THROWS(FlightComputer{c}, std::invalid_argument);
        }
        {
            ControllerConfig c;
            c.soc_thrust_stop = 0.05;  // not above safe_enter (0.10)
            CHECK_THROWS(FlightComputer{c}, std::invalid_argument);
        }
        {
            ControllerConfig c;
            c.soc_thrust_start = 1.1;
            CHECK_THROWS(FlightComputer{c}, std::invalid_argument);
        }
        {
            ControllerConfig c;
            c.min_density_start_kg_m3 = 1.0e-11;
            c.min_density_stop_kg_m3 = 2.0e-11;  // stop above start
            CHECK_THROWS(FlightComputer{c}, std::invalid_argument);
        }
        {
            ControllerConfig c;
            c.temperature_resume_k = 450.0;
            c.temperature_limit_k = 400.0;  // resume above limit
            CHECK_THROWS(FlightComputer{c}, std::invalid_argument);
        }
        {
            ControllerConfig c;
            c.temperature_limit_k = 500.0;
            c.temperature_fault_k = 450.0;  // limit above fault
            CHECK_THROWS(FlightComputer{c}, std::invalid_argument);
        }
        {
            ControllerConfig c;
            c.fault_hold_s = -1.0;
            CHECK_THROWS(FlightComputer{c}, std::invalid_argument);
        }
        {
            ControllerConfig c;
            c.soc_safe_exit = nan;
            CHECK_THROWS(FlightComputer{c}, std::invalid_argument);
        }
        FlightComputer ok(example_config());
        Flags f = ready();
        f.battery_fraction = 1.5;
        CHECK_THROWS(ok.update(f, 10.0), std::invalid_argument);
        f = ready();
        f.battery_fraction = nan;
        CHECK_THROWS(ok.update(f, 10.0), std::invalid_argument);
        f = ready();
        f.density_kg_m3 = -1.0e-11;
        CHECK_THROWS(ok.update(f, 10.0), std::invalid_argument);
        f = ready();
        f.temperature_k = nan;
        CHECK_THROWS(ok.update(f, 10.0), std::invalid_argument);
        CHECK_THROWS(ok.update(ready(), -1.0), std::invalid_argument);
        CHECK_THROWS(ok.update(ready(), nan), std::invalid_argument);
    }
    {
        TEST("flight: start-up goes SAFE, CHARGE, THRUST, one step at a time");
        FlightComputer c(example_config());
        CHECK(c.mode() == Mode::Safe);
        CHECK(c.update(ready(), 10.0) == Mode::Charge);  // fully charged, but one transition per step
        CHECK(c.update(ready(), 10.0) == Mode::Thrust);
        CHECK(c.update(ready(), 10.0) == Mode::Thrust);
        CHECK(c.mode() == Mode::Thrust);
    }
    {
        TEST("flight: SAFE leaves at 0.30 and not below; CHARGE falls back to SAFE below 0.10 and goes on at 0.50");
        Flags f = ready();
        f.battery_fraction = 0.2999;
        CHECK(next_from(Mode::Safe, f) == Mode::Safe);
        f.battery_fraction = 0.30;
        CHECK(next_from(Mode::Safe, f) == Mode::Charge);
        f.battery_fraction = 0.0999;
        CHECK(next_from(Mode::Charge, f) == Mode::Safe);
        f.battery_fraction = 0.10;
        CHECK(next_from(Mode::Charge, f) == Mode::Charge);
        f.battery_fraction = 0.4999;
        CHECK(next_from(Mode::Charge, f) == Mode::Charge);
        f.battery_fraction = 0.50;
        CHECK(next_from(Mode::Charge, f) == Mode::Thrust);
    }
    {
        TEST("flight: THRUST and COAST drop to CHARGE below 0.20, and to SAFE below 0.10, which takes priority");
        Flags f = ready();
        f.battery_fraction = 0.20;
        CHECK(next_from(Mode::Thrust, f) == Mode::Thrust);  // not below
        f.battery_fraction = 0.1999;
        CHECK(next_from(Mode::Thrust, f) == Mode::Charge);
        f.battery_fraction = 0.0999;
        CHECK(next_from(Mode::Thrust, f) == Mode::Safe);
        Flags blocked = ready();
        blocked.sunlit = false;  // COAST stays COAST while thrusting is blocked
        blocked.battery_fraction = 0.20;
        CHECK(next_from(Mode::Coast, blocked) == Mode::Coast);
        blocked.battery_fraction = 0.1999;
        CHECK(next_from(Mode::Coast, blocked) == Mode::Charge);
        blocked.battery_fraction = 0.0999;
        CHECK(next_from(Mode::Coast, blocked) == Mode::Safe);
    }
    {
        TEST("flight: after any stop, thrusting restarts only once the battery is back at the restart level");
        Flags f = ready();
        f.battery_fraction = 0.4999;
        CHECK(next_from(Mode::Coast, f) == Mode::Coast);    // everything else is fine, but the battery is not
        CHECK(next_from(Mode::Thrust, f) == Mode::Thrust);  // while a thruster already running goes on down to 0.20
        f.battery_fraction = 0.50;
        CHECK(next_from(Mode::Coast, f) == Mode::Thrust);
        // A coast through an eclipse, then sunlight: the battery climbs from 0.25 and THRUST waits for 0.50.
        FlightComputer c(example_config(), Mode::Coast);
        f = ready();
        f.battery_fraction = 0.25;
        CHECK(c.update(f, 10.0) == Mode::Coast);
        f.battery_fraction = 0.45;
        CHECK(c.update(f, 10.0) == Mode::Coast);
        f.battery_fraction = 0.50;
        CHECK(c.update(f, 10.0) == Mode::Thrust);
    }
    {
        TEST("flight: the thruster starts only if it is available, powered, in enough air and cool enough");
        // From CHARGE with a full battery: COAST if anything blocks the thruster, THRUST if nothing does.
        Flags f = ready();
        CHECK(next_from(Mode::Charge, f) == Mode::Thrust);
        f = ready();
        f.thruster_available = false;
        CHECK(next_from(Mode::Charge, f) == Mode::Coast);
        CHECK(next_from(Mode::Coast, f) == Mode::Coast);
        f = ready();
        f.sunlit = false;  // eclipse, with thrusting in eclipse off by default
        CHECK(next_from(Mode::Charge, f) == Mode::Coast);
        CHECK(next_from(Mode::Coast, f) == Mode::Coast);
        ControllerConfig eclipse = example_config();
        eclipse.thrust_in_eclipse = true;
        CHECK(next_from(Mode::Coast, f, eclipse) == Mode::Thrust);
        f = ready();
        f.density_kg_m3 = 1.9999e-11;  // just below the start density
        CHECK(next_from(Mode::Coast, f) == Mode::Coast);
        f.density_kg_m3 = 2.0e-11;
        CHECK(next_from(Mode::Coast, f) == Mode::Thrust);
        f = ready();
        f.temperature_k = 350.0001;  // just above the resume temperature
        CHECK(next_from(Mode::Coast, f) == Mode::Coast);
        f.temperature_k = 350.0;
        CHECK(next_from(Mode::Coast, f) == Mode::Thrust);
    }
    {
        TEST("flight: density, temperature and eclipse each stop the thruster, with hysteresis on the first two");
        Flags f = ready();
        f.density_kg_m3 = 1.5e-11;  // between stop (1e-11) and start (2e-11)
        CHECK(next_from(Mode::Thrust, f) == Mode::Thrust);  // already running: keeps going
        CHECK(next_from(Mode::Coast, f) == Mode::Coast);    // not running: does not start
        f.density_kg_m3 = 0.9999e-11;
        CHECK(next_from(Mode::Thrust, f) == Mode::Coast);
        f = ready();
        f.temperature_k = 380.0;  // between resume (350) and limit (400)
        CHECK(next_from(Mode::Thrust, f) == Mode::Thrust);
        CHECK(next_from(Mode::Coast, f) == Mode::Coast);
        f.temperature_k = 399.9999;
        CHECK(next_from(Mode::Thrust, f) == Mode::Thrust);
        f.temperature_k = 400.0;
        CHECK(next_from(Mode::Thrust, f) == Mode::Coast);
        f = ready();
        f.sunlit = false;
        CHECK(next_from(Mode::Thrust, f) == Mode::Coast);
        ControllerConfig eclipse = example_config();
        eclipse.thrust_in_eclipse = true;
        CHECK(next_from(Mode::Thrust, f, eclipse) == Mode::Thrust);
        f = ready();
        f.thruster_available = false;
        CHECK(next_from(Mode::Thrust, f) == Mode::Coast);
    }
    {
        TEST("flight: a fault from anywhere puts the machine in FAULT, and a temperature at the fault limit is one");
        Flags f = ready();
        f.fault = true;
        for (Mode m : {Mode::Safe, Mode::Charge, Mode::Coast, Mode::Thrust, Mode::Fault}) {
            CHECK(next_from(m, f) == Mode::Fault);
        }
        f = ready();
        f.temperature_k = 449.9999;  // above the thrust limit but below the fault limit
        CHECK(next_from(Mode::Thrust, f) == Mode::Coast);
        f.temperature_k = 450.0;
        for (Mode m : {Mode::Safe, Mode::Charge, Mode::Coast, Mode::Thrust}) {
            CHECK(next_from(m, f) == Mode::Fault);
        }
    }
    {
        TEST("flight: FAULT holds for the hold time after the fault clears, and a new fault restarts the count");
        FlightComputer c(example_config(), Mode::Thrust);
        Flags faulty = ready();
        faulty.fault = true;
        CHECK(c.update(faulty, 100.0) == Mode::Fault);
        CHECK(c.update(faulty, 100.0) == Mode::Fault);  // still asserted
        for (int i = 1; i <= 5; ++i) {                   // 500 s clear of 600 s needed
            CHECK(c.update(ready(), 100.0) == Mode::Fault);
            CHECK_NEAR(c.fault_clear_s(), 100.0 * i, 1.0e-12);
        }
        CHECK(c.update(faulty, 100.0) == Mode::Fault);  // asserted again: the count starts over
        CHECK_EQ(c.fault_clear_s(), 0.0);
        for (int i = 1; i <= 5; ++i) CHECK(c.update(ready(), 100.0) == Mode::Fault);
        CHECK(c.update(ready(), 100.0) == Mode::Safe);  // the sixth clear step reaches 600 s
        CHECK_EQ(c.fault_clear_s(), 0.0);
        CHECK(c.update(ready(), 100.0) == Mode::Charge);  // and it works its way up again
        CHECK(c.update(ready(), 100.0) == Mode::Thrust);

        ControllerConfig no_hold = example_config();
        no_hold.fault_hold_s = 0.0;
        FlightComputer d(no_hold, Mode::Fault);
        CHECK(d.update(ready(), 10.0) == Mode::Safe);  // no hold: SAFE on the first clear step
    }
    {
        TEST("flight: every mode and combination of flags follows the rules (53,760 cases against a reference)");
        const Mode modes[] = {Mode::Safe, Mode::Charge, Mode::Coast, Mode::Thrust, Mode::Fault};
        const double socs[] = {0.0, 0.05, 0.0999, 0.10, 0.15, 0.1999, 0.20, 0.25, 0.30, 0.40, 0.4999, 0.50, 0.60, 1.0};
        const double densities[] = {0.0, 0.5e-11, 1.0e-11, 1.5e-11, 2.0e-11, 5.0e-11};
        const double temperatures[] = {0.0, 300.0, 350.0, 380.0, 400.0, 449.0, 450.0, 500.0};
        long cases = 0;
        long disagreements = 0;
        long illegal = 0;
        for (const bool eclipse_policy : {false, true}) {
            ControllerConfig c = example_config();
            c.thrust_in_eclipse = eclipse_policy;
            for (const Mode from : modes) {
                for (const double soc : socs) {
                    for (const bool sunlit : {false, true}) {
                        for (const double density : densities) {
                            for (const double temperature : temperatures) {
                                for (const bool fault : {false, true}) {
                                    for (const bool available : {false, true}) {
                                        Flags f;
                                        f.battery_fraction = soc;
                                        f.sunlit = sunlit;
                                        f.density_kg_m3 = density;
                                        f.temperature_k = temperature;
                                        f.fault = fault;
                                        f.thruster_available = available;
                                        const Mode got = next_from(from, f, c);
                                        ++cases;
                                        if (got != reference_next(from, f, c)) ++disagreements;
                                        // Properties that hold whatever the rules say: SAFE leads only to CHARGE (or
                                        // FAULT), FAULT leads only to SAFE, THRUST is never entered or kept without a
                                        // thruster, power, air, a cool thruster and a battery above the stop level, and
                                        // is never entered from another mode below the restart level.
                                        const bool fault_now = fault || temperature >= c.temperature_fault_k;
                                        if (from == Mode::Safe && got != Mode::Safe && got != Mode::Charge && got != Mode::Fault) ++illegal;
                                        if (from == Mode::Fault && got != Mode::Fault && got != Mode::Safe) ++illegal;
                                        if (from != Mode::Fault && fault_now != (got == Mode::Fault)) ++illegal;
                                        if (got == Mode::Thrust && !(available && (sunlit || eclipse_policy) &&
                                                                     soc >= c.soc_thrust_stop && density >= c.min_density_stop_kg_m3 &&
                                                                     temperature < c.temperature_limit_k && from != Mode::Safe)) {
                                            ++illegal;
                                        }
                                        if (got == Mode::Thrust && from != Mode::Thrust && soc < c.soc_thrust_start) ++illegal;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        CHECK_EQ(cases, 53760L);
        CHECK_EQ(disagreements, 0L);
        CHECK_EQ(illegal, 0L);
    }
    {
        TEST("flight: the same flags in the same order give the same modes");
        const double socs[] = {0.9, 0.7, 0.45, 0.25, 0.15, 0.05, 0.2, 0.35, 0.6, 0.95};
        Mode first[10];
        Mode second[10];
        for (int pass = 0; pass < 2; ++pass) {
            FlightComputer c(example_config());
            for (int i = 0; i < 10; ++i) {
                Flags f = ready();
                f.battery_fraction = socs[i];
                f.sunlit = (i % 3) != 1;
                (pass == 0 ? first : second)[i] = c.update(f, 10.0);
            }
        }
        for (int i = 0; i < 10; ++i) CHECK(first[i] == second[i]);
    }
}
