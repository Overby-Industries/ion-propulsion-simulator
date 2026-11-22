#include "check.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

#include "abep/drag.hpp"
#include "abep/thrust.hpp"
#include "abep/units.hpp"

namespace {

using abep::IntakeThruster;
using abep::ThrustLimit;

// Relative agreement. 1e-14 is a few unit roundoffs of a product of four factors: these checks compare the
// code with the same arithmetic done on paper, so only rounding can separate them.
bool near_rel(double a, double b, double tol) { return std::fabs(a - b) <= tol * std::fabs(b); }

// Illustrative values chosen so the arithmetic can be done by hand. They are not data about any thruster.
IntakeThruster example() { return {abep::units::mn_per_kw_to_n_per_w(13.7), 20.0e3, 1.0, 0.35}; }

}  // namespace

void runThrustTests() {
    const IntakeThruster t = example();
    const double rho = 1.0e-10;   // kg/m^3
    const double v = 7500.0;      // m/s
    // Collected mass flow: 0.35 * 1e-10 * 7500 * 1 = 2.625e-7 kg/s. Propellant-limited thrust: 2.625e-7 * 2e4 = 5.25e-3 N.
    // Power-limited thrust: 1.37e-5 N/W times P. They cross at P = 5.25e-3 / 1.37e-5 = 383.2117 W.

    {
        TEST("thrust: the collected mass flow is eta_c * rho * v * A_in");
        CHECK(near_rel(abep::collectable_mass_flow(t, rho, v), 2.625e-7, 1.0e-14));
        CHECK_EQ(abep::collectable_mass_flow(t, 0.0, v), 0.0);
        CHECK_EQ(abep::collectable_mass_flow(t, rho, 0.0), 0.0);
    }
    {
        TEST("thrust: below the crossover power, power sets the thrust");
        const auto r = abep::abep_thrust(t, 300.0, rho, v);
        CHECK(r.limit == ThrustLimit::Power);
        CHECK(near_rel(r.thrust_n, 4.11e-3, 1.0e-14));  // 1.37e-5 * 300
        CHECK(near_rel(r.power_w, 300.0, 1.0e-14));
        CHECK(near_rel(r.mass_flow_kg_s, 2.055e-7, 1.0e-14));  // 4.11e-3 / 2e4
        CHECK(r.mass_flow_kg_s < r.collectable_mass_flow_kg_s);
    }
    {
        TEST("thrust: above the crossover power, the intake sets the thrust and the power drawn falls short");
        const auto r = abep::abep_thrust(t, 500.0, rho, v);
        CHECK(r.limit == ThrustLimit::Propellant);
        CHECK(near_rel(r.thrust_n, 5.25e-3, 1.0e-14));
        CHECK(near_rel(r.power_w, 5.25e-3 / 1.37e-5, 1.0e-14));  // 383.2 W of the 500 W offered
        CHECK(r.power_w < 500.0);
        CHECK(near_rel(r.mass_flow_kg_s, r.collectable_mass_flow_kg_s, 1.0e-14));  // all the air is used
    }
    {
        TEST("thrust: continuous across the crossover power");
        const double crossover = 2.625e-7 * 2.0e4 / 1.37e-5;
        const auto below = abep::abep_thrust(t, crossover * (1.0 - 1.0e-9), rho, v);
        const auto above = abep::abep_thrust(t, crossover * (1.0 + 1.0e-9), rho, v);
        CHECK(below.limit == ThrustLimit::Power);
        CHECK(above.limit == ThrustLimit::Propellant);
        CHECK(near_rel(below.thrust_n, above.thrust_n, 2.0e-9));  // they differ by the 2e-9 step in power, and no more
    }
    {
        TEST("thrust: no power, no air, no intake or no speed gives no thrust");
        CHECK_EQ(abep::abep_thrust(t, 0.0, rho, v).thrust_n, 0.0);
        CHECK(abep::abep_thrust(t, 500.0, 0.0, v).limit == ThrustLimit::Propellant);
        CHECK_EQ(abep::abep_thrust(t, 500.0, 0.0, v).thrust_n, 0.0);
        CHECK_EQ(abep::abep_thrust(t, 500.0, rho, 0.0).thrust_n, 0.0);
        IntakeThruster closed = t;
        closed.intake_area_m2 = 0.0;
        CHECK_EQ(abep::abep_thrust(closed, 500.0, rho, v).thrust_n, 0.0);
        closed = t;
        closed.capture_efficiency = 0.0;
        CHECK_EQ(abep::abep_thrust(closed, 500.0, rho, v).thrust_n, 0.0);
    }
    {
        TEST("thrust: scales linearly with power when power-limited, and with density, area, efficiency and speed when not");
        const double p = 100.0;  // well below the crossover at this density
        CHECK(near_rel(abep::abep_thrust(t, 2.0 * p, rho, v).thrust_n, 2.0 * abep::abep_thrust(t, p, rho, v).thrust_n, 1.0e-14));
        const double big = 5000.0;  // well above it
        const double base = abep::abep_thrust(t, big, rho, v).thrust_n;
        CHECK(near_rel(abep::abep_thrust(t, big, 2.0 * rho, v).thrust_n, 2.0 * base, 1.0e-14));
        CHECK(near_rel(abep::abep_thrust(t, big, rho, 2.0 * v).thrust_n, 2.0 * base, 1.0e-14));
        IntakeThruster doubled = t;
        doubled.intake_area_m2 *= 2.0;
        CHECK(near_rel(abep::abep_thrust(doubled, big, rho, v).thrust_n, 2.0 * base, 1.0e-14));
        doubled = t;
        doubled.capture_efficiency *= 2.0;
        CHECK(near_rel(abep::abep_thrust(doubled, big, rho, v).thrust_n, 2.0 * base, 1.0e-14));
        doubled = t;
        doubled.exhaust_velocity_m_s *= 2.0;
        CHECK(near_rel(abep::abep_thrust(doubled, big, rho, v).thrust_n, 2.0 * base, 1.0e-14));  // twice the speed, twice the thrust per kg
    }
    {
        TEST("thrust: whether the intake can supply a given drag does not depend on the density");
        // F_m / D = (eta_c rho v A_in c_e) / (1/2 rho v^2 Cd A) = 2 eta_c (A_in / A) c_e / (Cd v): rho cancels.
        const double cd = 2.2;
        const double area = 1.0;
        const double expected = 2.0 * t.capture_efficiency * (t.intake_area_m2 / area) * t.exhaust_velocity_m_s / (cd * v);
        for (const double density : {1.0e-12, 1.0e-10, 1.0e-8}) {
            const double supplied = abep::collectable_mass_flow(t, density, v) * t.exhaust_velocity_m_s;
            CHECK(near_rel(supplied / abep::drag_force(density, v, cd, area), expected, 1.0e-14));
        }
    }
    {
        TEST("thrust: jet power is the implied thruster efficiency times the electrical power drawn");
        // 1/2 F c_e = (1/2 tau c_e) * (F / tau): energy is conserved, with the rest lost as heat. The efficiency here is 13.7%.
        CHECK(near_rel(abep::implied_thruster_efficiency(t), 0.137, 1.0e-14));
        for (const double p : {10.0, 300.0, 383.0, 384.0, 5000.0}) {
            for (const double density : {1.0e-11, 1.0e-10, 1.0e-9}) {
                const auto r = abep::abep_thrust(t, p, density, v);
                CHECK(near_rel(0.5 * r.thrust_n * t.exhaust_velocity_m_s, abep::implied_thruster_efficiency(t) * r.power_w, 1.0e-14));
                CHECK(r.power_w <= p * (1.0 + 1.0e-15));
                CHECK(r.mass_flow_kg_s <= r.collectable_mass_flow_kg_s * (1.0 + 1.0e-15));
            }
        }
    }
    {
        TEST("thrust: inputs that are invalid, or that violate energy conservation, are refused");
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();
        auto with = [&](auto change) {
            IntakeThruster x = t;
            change(x);
            return x;
        };
        CHECK_THROWS(abep::validate(with([](IntakeThruster& x) { x.thrust_density_n_per_w = 0.0; })), std::invalid_argument);
        CHECK_THROWS(abep::validate(with([](IntakeThruster& x) { x.thrust_density_n_per_w = -1.0e-5; })), std::invalid_argument);
        CHECK_THROWS(abep::validate(with([&](IntakeThruster& x) { x.thrust_density_n_per_w = nan; })), std::invalid_argument);
        CHECK_THROWS(abep::validate(with([](IntakeThruster& x) { x.exhaust_velocity_m_s = 0.0; })), std::invalid_argument);
        CHECK_THROWS(abep::validate(with([&](IntakeThruster& x) { x.exhaust_velocity_m_s = inf; })), std::invalid_argument);
        CHECK_THROWS(abep::validate(with([](IntakeThruster& x) { x.intake_area_m2 = -0.1; })), std::invalid_argument);
        CHECK_THROWS(abep::validate(with([](IntakeThruster& x) { x.capture_efficiency = 1.01; })), std::invalid_argument);
        CHECK_THROWS(abep::validate(with([](IntakeThruster& x) { x.capture_efficiency = -0.01; })), std::invalid_argument);
        CHECK_THROWS(abep::validate(with([&](IntakeThruster& x) { x.capture_efficiency = nan; })), std::invalid_argument);
        // tau * c_e = 2 is the limit: 1.37e-5 N/W with 146.1 km/s would be a thruster that is more than 100% efficient.
        CHECK_THROWS(abep::validate(with([](IntakeThruster& x) { x.exhaust_velocity_m_s = 146.0e3; })), std::invalid_argument);
        CHECK(abep::implied_thruster_efficiency(with([](IntakeThruster& x) { x.exhaust_velocity_m_s = 145.0e3; })) < 1.0);
        CHECK_THROWS(abep::abep_thrust(t, -1.0, rho, v), std::invalid_argument);
        CHECK_THROWS(abep::abep_thrust(t, nan, rho, v), std::invalid_argument);
        CHECK_THROWS(abep::abep_thrust(t, 100.0, -1.0e-10, v), std::invalid_argument);
        CHECK_THROWS(abep::abep_thrust(t, 100.0, rho, -1.0), std::invalid_argument);
        CHECK_THROWS(abep::abep_thrust(with([](IntakeThruster& x) { x.capture_efficiency = 2.0; }), 100.0, rho, v), std::invalid_argument);
    }
}
