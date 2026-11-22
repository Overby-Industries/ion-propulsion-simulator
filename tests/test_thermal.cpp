#include "check.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

#include "abep/thermal.hpp"

namespace {

// Relative agreement; 1e-14 is a few unit roundoffs of the few operations involved.
bool near_rel(double a, double b, double tol) { return std::fabs(a - b) <= tol * std::fabs(b); }

// Illustrative numbers, chosen so the time constant C / G is a round 1000 s and 500 W settles 100 K above the sink. They
// are not data about any thruster.
abep::ThrusterThermal example() { return {5000.0, 5.0, 300.0}; }

}  // namespace

void runThermalTests() {
    const abep::ThrusterThermal p = example();
    {
        TEST("thermal: the steady state is the sink temperature plus the heat over the conductance");
        CHECK_EQ(abep::steady_state_temperature(p, 0.0), 300.0);
        CHECK(near_rel(abep::steady_state_temperature(p, 500.0), 400.0, 1.0e-15));  // 300 + 500 / 5
        // Many time constants at a constant heat input end there: after 40 of them, exp(-40) = 4e-18 of the gap is left.
        double temperature = 300.0;
        for (int i = 0; i < 40; ++i) temperature = abep::thermal_step(p, temperature, 500.0, 1000.0);
        CHECK_NEAR(temperature, 400.0, 1.0e-12);
    }
    {
        TEST("thermal: after one time constant the node has covered 1 - 1/e of the way");
        // tau = C / G = 1000 s.
        CHECK(near_rel(abep::thermal_step(p, 300.0, 500.0, 1000.0), 300.0 + 100.0 * (1.0 - std::exp(-1.0)), 1.0e-14));
        CHECK(near_rel(abep::thermal_step(p, 400.0, 0.0, 1000.0), 300.0 + 100.0 * std::exp(-1.0), 1.0e-14));  // cooling
        CHECK_EQ(abep::thermal_step(p, 330.0, 250.0, 0.0), 330.0);  // no time, no change
        CHECK_EQ(abep::thermal_step(p, 350.0, 250.0, 1.0e6), 350.0);  // 300 + 250 / 5 = 350: already at balance, stays there
    }
    {
        TEST("thermal: any split of a time span into steps gives the same temperature");
        // The step is exact, so splitting changes the answer only by rounding: a few unit roundoffs of 400 K, 5.7e-14
        // each; 1e-10 K is far above that.
        const double whole = abep::thermal_step(p, 320.0, 600.0, 1500.0);
        const double two = abep::thermal_step(p, abep::thermal_step(p, 320.0, 600.0, 400.0), 600.0, 1100.0);
        double many = 320.0;
        for (int i = 0; i < 150; ++i) many = abep::thermal_step(p, many, 600.0, 10.0);
        CHECK_NEAR(two, whole, 1.0e-10);
        CHECK_NEAR(many, whole, 1.0e-10);
    }
    {
        TEST("thermal: agrees with RK4 integration of C dT/dt = Q - G (T - T_sink)");
        // 3000 steps of 1 s. RK4's truncation error here is about 1e-15 K per step and rounding about 1e-13 K, so
        // 1e-9 K is a bound with room, not a fit.
        const double q = 700.0;
        double rk = 310.0;
        const auto rate = [&](double temperature) { return (q - p.conductance_w_per_k * (temperature - p.sink_temperature_k)) / p.heat_capacity_j_per_k; };
        for (int i = 0; i < 3000; ++i) {
            const double k1 = rate(rk);
            const double k2 = rate(rk + 0.5 * k1);
            const double k3 = rate(rk + 0.5 * k2);
            const double k4 = rate(rk + k3);
            rk += (k1 + 2.0 * k2 + 2.0 * k3 + k4) / 6.0;
        }
        CHECK_NEAR(abep::thermal_step(p, 310.0, q, 3000.0), rk, 1.0e-9);
    }
    {
        TEST("thermal: with no cooling the temperature rises linearly");
        const abep::ThrusterThermal adiabatic{5000.0, 0.0, 300.0};
        CHECK(near_rel(abep::thermal_step(adiabatic, 300.0, 100.0, 50.0), 301.0, 1.0e-14));  // 100 W * 50 s / 5000 J/K = 1 K
        CHECK_EQ(abep::thermal_step(adiabatic, 320.0, 0.0, 1.0e6), 320.0);  // nothing in, nothing out
        CHECK_THROWS(abep::steady_state_temperature(adiabatic, 100.0), std::invalid_argument);
    }
    {
        TEST("thermal: invalid inputs are refused");
        const double nan = std::numeric_limits<double>::quiet_NaN();
        CHECK_THROWS(abep::thermal_step(abep::ThrusterThermal{0.0, 5.0, 300.0}, 300.0, 1.0, 1.0), std::invalid_argument);
        CHECK_THROWS(abep::thermal_step(abep::ThrusterThermal{5000.0, -1.0, 300.0}, 300.0, 1.0, 1.0), std::invalid_argument);
        CHECK_THROWS(abep::thermal_step(abep::ThrusterThermal{5000.0, 5.0, 0.0}, 300.0, 1.0, 1.0), std::invalid_argument);
        CHECK_THROWS(abep::thermal_step(abep::ThrusterThermal{nan, 5.0, 300.0}, 300.0, 1.0, 1.0), std::invalid_argument);
        CHECK_THROWS(abep::thermal_step(p, 0.0, 1.0, 1.0), std::invalid_argument);
        CHECK_THROWS(abep::thermal_step(p, 300.0, -1.0, 1.0), std::invalid_argument);
        CHECK_THROWS(abep::thermal_step(p, 300.0, 1.0, -1.0), std::invalid_argument);
        CHECK_THROWS(abep::thermal_step(p, nan, 1.0, 1.0), std::invalid_argument);
        CHECK_THROWS(abep::steady_state_temperature(p, -1.0), std::invalid_argument);
    }
}
