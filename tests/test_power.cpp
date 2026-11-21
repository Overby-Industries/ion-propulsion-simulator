#include "check.hpp"

#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>

#include "abep/constants.hpp"
#include "abep/eclipse.hpp"
#include "abep/orbit.hpp"
#include "abep/power.hpp"
#include "abep/units.hpp"

namespace {

// Relative agreement; 1e-14 is a few unit roundoffs of a product of three factors.
bool near_rel(double a, double b, double tol) { return std::fabs(a - b) <= tol * std::fabs(b); }

}  // namespace

void runPowerTests() {
    {
        TEST("solar array: irradiance * area * efficiency * cos(incidence)");
        CHECK_EQ(abep::constants::kSolarConstant, 1361.0);  // IAU 2015 Resolution B3 nominal value
        CHECK(near_rel(abep::solar_array_power(1.0, 0.295), 401.495, 1.0e-14));  // 1361 * 0.295
        CHECK(near_rel(abep::solar_array_power(4.0, 0.295, 0.5), 4.0 * 0.5 * 401.495, 1.0e-14));
        CHECK(near_rel(abep::solar_array_power(2.0, 0.3, 1.0, 1000.0), 600.0, 1.0e-14));
        CHECK_EQ(abep::solar_array_power(1.0, 0.295, 0.0), 0.0);
        CHECK_EQ(abep::solar_array_power(1.0, 0.295, -0.5), 0.0);  // the Sun is behind the array
        CHECK_EQ(abep::solar_array_power(0.0, 0.295), 0.0);
        CHECK_THROWS(abep::solar_array_power(-1.0, 0.3), std::invalid_argument);
        CHECK_THROWS(abep::solar_array_power(1.0, 1.1), std::invalid_argument);
        CHECK_THROWS(abep::solar_array_power(1.0, 0.3, 1.5), std::invalid_argument);
        CHECK_THROWS(abep::solar_array_power(1.0, 0.3, 1.0, -1.0), std::invalid_argument);
    }
    {
        TEST("battery: charges, discharges, saturates, and reports the change it made");
        abep::Battery b(100.0, 50.0);
        CHECK_NEAR(b.apply(3600.0, 36.0), 36.0, 1.0e-12);  // 3600 W for 36 s is 36 Wh
        CHECK_NEAR(b.energy_wh(), 86.0, 1.0e-12);
        CHECK_NEAR(b.apply(3600.0, 36.0), 14.0, 1.0e-12);  // only 14 Wh of room
        CHECK_EQ(b.energy_wh(), 100.0);
        CHECK_EQ(b.apply(10.0, 60.0), 0.0);  // full: nothing accepted
        CHECK_NEAR(b.apply(-3600.0, 18.0), -18.0, 1.0e-12);
        CHECK_NEAR(b.apply(-3600.0, 3600.0), -82.0, 1.0e-12);  // asks for 3600 Wh, has 82
        CHECK_EQ(b.energy_wh(), 0.0);
        CHECK_EQ(b.apply(-5.0, 10.0), 0.0);  // empty: nothing supplied
        CHECK_EQ(b.apply(1000.0, 0.0), 0.0);  // no time, no change
        CHECK_EQ(b.energy_wh(), 0.0);
    }
    {
        TEST("battery: stays within its limits and accounts for every watt-hour");
        abep::Battery b(50.0, 25.0);
        double total = 0.0;
        for (int i = 0; i < 1000; ++i) {
            const double power = 800.0 * std::sin(0.37 * static_cast<double>(i)) + 150.0;  // a deterministic wobble
            total += b.apply(power, 30.0);
            CHECK(b.energy_wh() >= 0.0 && b.energy_wh() <= 50.0);
        }
        CHECK_NEAR(b.energy_wh() - 25.0, total, 1.0e-9);  // rounding over 1000 additions is about 1e-11
    }
    {
        TEST("battery: invalid construction and use are refused");
        const double nan = std::numeric_limits<double>::quiet_NaN();
        CHECK_THROWS(abep::Battery(0.0, 0.0), std::invalid_argument);
        CHECK_THROWS(abep::Battery(-5.0, 0.0), std::invalid_argument);
        CHECK_THROWS(abep::Battery(nan, 0.0), std::invalid_argument);
        CHECK_THROWS(abep::Battery(10.0, -1.0), std::invalid_argument);
        CHECK_THROWS(abep::Battery(10.0, 10.5), std::invalid_argument);
        abep::Battery b(10.0, 5.0);
        CHECK_THROWS(b.apply(nan, 1.0), std::invalid_argument);
        CHECK_THROWS(b.apply(1.0, -1.0), std::invalid_argument);
    }
    {
        TEST("power budget: the sustainable thrust duty");
        CHECK_NEAR(abep::sustainable_thrust_duty(3000.0, 0.6, 500.0, 2000.0), 0.65, 1.0e-15);  // (1800 - 500) / 2000
        CHECK_EQ(abep::sustainable_thrust_duty(3000.0, 1.0, 100.0, 1000.0), 1.0);  // more than the thruster can use
        CHECK_EQ(abep::sustainable_thrust_duty(1000.0, 0.5, 600.0, 2000.0), 0.0);  // the bus load takes it all
        CHECK_THROWS(abep::sustainable_thrust_duty(-1.0, 0.5, 100.0, 1000.0), std::invalid_argument);
        CHECK_THROWS(abep::sustainable_thrust_duty(1000.0, 1.1, 100.0, 1000.0), std::invalid_argument);
        CHECK_THROWS(abep::sustainable_thrust_duty(1000.0, 0.5, -1.0, 1000.0), std::invalid_argument);
        CHECK_THROWS(abep::sustainable_thrust_duty(1000.0, 0.5, 100.0, 0.0), std::invalid_argument);
    }
    {
        TEST("power budget: a propagated orbit, the eclipse, the array and the battery agree with the closed-form duty");
        // Tolerances fixed before running. Energy is conserved to 1e-9 of the energy generated (rounding over 22000
        // sums). The thruster duty is within 1% of the closed form: that is a bound, not a fit, because the battery can
        // end up to its capacity (800 Wh) away from where it started, and 800 Wh is 0.65% of the thruster energy over
        // 40 orbits at 2000 W (123 kWh). The thruster runs only when the battery would stay above a reserve that
        // carries the bus through the longest eclipse (500 W for 34.5 min, 288 Wh).
        const double altitude = 400.0e3;
        const double a = abep::constants::kEarthEquatorialRadius + altitude;
        const double period = abep::orbital_period_two_body(a);
        const double beta = abep::units::deg_to_rad(30.0);
        const abep::Vec3 sun = abep::sun_direction_from_ra_dec(0.0, 0.0);
        abep::State s = abep::circular_orbit_state(altitude, abep::units::deg_to_rad(90.0), beta, 0.0);
        const auto two_body = [](double, const abep::State& st) { return abep::gravity_acceleration(st.r, false); };
        const double array_w = 3000.0;
        const double load_w = 500.0;
        const double thruster_w = 2000.0;
        const double dt = 10.0;
        const double hours = dt / 3600.0;
        const double reserve_wh = 320.0;
        abep::Battery battery(800.0, reserve_wh);
        const long long steps = static_cast<long long>(40.0 * period / dt);
        double generated = 0.0;
        double to_load = 0.0;
        double to_thruster = 0.0;
        double refused = 0.0;
        for (long long i = 0; i < steps; ++i) {
            const double generation = abep::in_earth_shadow(s.r, sun) ? 0.0 : array_w;
            const bool thrust = battery.energy_wh() + (generation - load_w - thruster_w) * hours >= reserve_wh;
            const double net = generation - load_w - (thrust ? thruster_w : 0.0);
            refused += net * hours - battery.apply(net, dt);
            generated += generation * hours;
            to_load += load_w * hours;
            if (thrust) to_thruster += thruster_w * hours;
            s = abep::rk4_step(s, static_cast<double>(i) * dt, dt, two_body);
        }
        CHECK_NEAR(generated - to_load - to_thruster, (battery.energy_wh() - reserve_wh) + refused, 1.0e-9 * generated);
        CHECK_NEAR(refused, 0.0, 1.0e-6);  // the battery was never full and never empty, so it refused nothing
        const double duty = to_thruster / (thruster_w * static_cast<double>(steps) * hours);
        const double closed_form =
            abep::sustainable_thrust_duty(array_w, 1.0 - abep::circular_orbit_eclipse_fraction(a, beta), load_w, thruster_w);
        CHECK_NEAR(duty, closed_form, 0.01);
        std::printf("    thruster duty over 40 orbits %.4f, closed form %.4f\n", duty, closed_form);
    }
}
