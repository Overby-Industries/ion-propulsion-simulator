#include "check.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "abep/atmosphere.hpp"
#include "abep/drag.hpp"
#include "abep/full_run.hpp"
#include "abep/mission.hpp"
#include "abep/orbit.hpp"
#include "abep/run_config.hpp"
#include "abep/standard_atmosphere_1976.hpp"
#include "abep/thrust.hpp"
#include "abep/units.hpp"

#ifndef ABEP_DATA_DIR
#error "ABEP_DATA_DIR must name the repository's data directory; CMakeLists.txt sets it"
#endif

// Validation cases 25 to 28 (docs/VALIDATION.md): the model against numbers that published studies state, as far as it can be
// compared. GOCE's decay rate and thrust range come from ESA's pages; the other studies are as Romano et al., Acta Astronautica 147
// (2018) 114-126, quotes them in its review (Sect. 1), whose primary papers have not been read. The tolerances were written in
// docs/VALIDATION.md before these tests were run. Where a test only measures a gap, it says so.

namespace {

namespace units = abep::units;

constexpr double kCd = 2.2;  // Romano et al.'s drag coefficient; GOCE's own is not stated in the pages read

// The paper's atmosphere (Fig. 3, NRLMSISE-00, F10.7 = 140) as a density table: the first two columns of the data file.
bool readPaperDensity(const std::string& path, std::vector<abep::DensityTable::Node>& nodes) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    bool header_seen = false;
    while (std::getline(in, line)) {
        if (line.find_first_not_of(" \t\r") == std::string::npos || line[0] == '#') continue;
        if (!header_seen) {
            header_seen = true;
            continue;
        }
        std::replace(line.begin(), line.end(), ',', ' ');
        std::istringstream fields(line);
        double altitude_km = 0.0;
        double density = 0.0;
        if (!(fields >> altitude_km >> density)) return false;
        nodes.push_back({altitude_km * 1.0e3, density});
    }
    return !nodes.empty();
}

// The drag force, N, on a satellite in a circular orbit of the given altitude (m) and inclination (rad). A satellite of 1 kg, so that
// the acceleration in m/s^2 is the force in N.
double dragNewtons(double altitude_m, double inclination_rad, double density, double area_m2, double drag_coefficient = kCd) {
    const abep::State s = abep::circular_orbit_state(altitude_m, inclination_rad);
    return abep::norm(abep::drag_acceleration(s, density, drag_coefficient, area_m2, 1.0, true));
}

}  // namespace

void runPublishedTests() {
    const abep::DensityTable ussa = abep::ussa1976::density_table();

    {
        TEST("GOCE: unpowered, the satellite near 280 km decays at about the published 190 m/day, to within a factor of 3");
        // ESA, 6 April 2009: "approximately 280 km, gently decaying at a rate of about 190 m/day"; 1050 kg, 1.1 m^2, 96.7 deg.
        // Band fixed before running: 190/3 to 190*3 m/day (docs/VALIDATION.md, case 25). It measures the gap between a mean-activity
        // atmosphere and the solar minimum of 2009 more than it validates anything.
        abep::RunSettings s;
        s.altitude_km = 280.0;
        s.inclination_deg = 96.7;
        s.mass_kg = 1050.0;
        s.frontal_area_m2 = 1.1;
        s.drag_coefficient = kCd;
        s.thruster_enabled = 0.0;
        s.duration_days = 3.0;
        s.sample_every = 10.0;  // a sample every 100 s
        const abep::Mission m = abep::make_mission(s);
        const abep::MissionResult run = abep::run_mission(m.satellite, m.scenario, m.controller, ussa);
        CHECK(run.end_reason == abep::EndReason::Completed);
        const double period = abep::orbital_period_two_body(abep::semi_major_axis(m.scenario.initial));
        const std::size_t orbits = static_cast<std::size_t>(m.scenario.duration_s / period);
        CHECK(orbits >= 40);
        // The mean radius over each orbit (a window of one two-body period), and the slope of those means against time.
        std::vector<double> sum(orbits, 0.0);
        std::vector<int> count(orbits, 0);
        for (const abep::Sample& sample : run.samples) {
            const std::size_t k = static_cast<std::size_t>(sample.t_s / period);
            if (k >= orbits) continue;
            sum[k] += abep::norm(sample.position_m);
            ++count[k];
        }
        double sx = 0.0;
        double sy = 0.0;
        double sxx = 0.0;
        double sxy = 0.0;
        for (std::size_t k = 0; k < orbits; ++k) {
            CHECK(count[k] >= 50);
            const double x = (static_cast<double>(k) + 0.5) * period;
            const double y = sum[k] / static_cast<double>(std::max(count[k], 1));
            sx += x;
            sy += y;
            sxx += x * x;
            sxy += x * y;
        }
        const double n = static_cast<double>(orbits);
        const double rate_m_per_day = -86400.0 * (n * sxy - sx * sy) / (n * sxx - sx * sx);  // positive when the orbit shrinks
        std::printf("    decay rate in the 1976 standard atmosphere: %.0f m/day, against the published 190 (ratio %.2f)\n",
                    rate_m_per_day, rate_m_per_day / 190.0);
        CHECK(rate_m_per_day > 190.0 / 3.0 && rate_m_per_day < 190.0 * 3.0);
    }
    {
        TEST("GOCE: the drag where it flew lies inside the ion engine's published thrust range, 1 to 20 mN");
        // ESA: thrust "between 1 and 20 mN"; 254 km from September 2009 to July 2012, 224 km from May 2013; Romano et al.: 1.1 m^2,
        // 235 to 265 km, 1.5 to 20 mN. The published range is the thing compared: no percentage.
        const double inclination = units::deg_to_rad(96.7);
        for (const double km : {224.0, 235.0, 254.0, 265.0}) {
            const double drag = dragNewtons(km * 1.0e3, inclination, ussa.density(km * 1.0e3), 1.1);
            std::printf("    %.0f km: drag %.2f mN\n", km, drag * 1.0e3);
            CHECK(drag >= 1.0e-3 && drag <= 20.0e-3);
        }
    }
    {
        TEST("ESA demonstrator: the drag at 200 km is inside its thrust range, and its best thrust per power holds the orbit within 1 kW");
        // Romano et al. Sect. 1, the ESA study [32]: 1000 kg, 1 m^2, 200 km sun-synchronous, up to 1 kW for propulsion, thrust 2 to
        // 20 mN. 20 mN/kW is the 20 mN of that 1 kW. Neither the exhaust velocity nor the intake is stated; they are set so that
        // the intake cannot be what limits.
        abep::RunSettings s;
        s.altitude_km = 200.0;
        s.inclination_deg = 96.3;  // sun-synchronous at 200 km
        s.mass_kg = 1000.0;
        s.frontal_area_m2 = 1.0;
        s.drag_coefficient = kCd;
        s.thrust_density_mn_per_kw = 20.0;
        s.exhaust_velocity_km_s = 40.0;
        s.intake_area_m2 = 20.0;
        s.capture_efficiency = 1.0;
        s.duration_days = 0.1;
        const abep::FullRun r = abep::simulate(s, ussa);
        CHECK(r.have_hold);
        if (r.have_hold) {
            std::printf("    mean drag %.2f mN; power to hold at 20 mN/kW: %.0f W\n", r.hold.mean_drag_n * 1.0e3, r.hold.power_w);
            CHECK(r.hold.status == abep::HoldResult::Status::Holds);
            CHECK(r.hold.mean_drag_n >= 2.0e-3 && r.hold.mean_drag_n <= 20.0e-3);
            CHECK(r.hold.power_w <= 1000.0);
        }
    }
    {
        TEST("Pekker and Keidar: 22 N and 9.1 N at 13 mN/kW need the powers Romano et al. quote, 1.6 to 2 MW and 700 to 800 kW");
        // A check of the definition and the units of the thrust density, not of any physics. The intake is huge, so that it cannot
        // limit.
        const abep::IntakeThruster t{units::mn_per_kw_to_n_per_w(13.0), 40.0e3, 1.0e6, 1.0};
        const double p90 = 22.0 / t.thrust_density_n_per_w;
        const double p95 = 9.1 / t.thrust_density_n_per_w;
        const abep::ThrustResult at90 = abep::abep_thrust(t, p90, 1.0e-6, 7.8e3);
        const abep::ThrustResult at95 = abep::abep_thrust(t, p95, 1.0e-7, 7.8e3);
        CHECK(at90.limit == abep::ThrustLimit::Power && at95.limit == abep::ThrustLimit::Power);
        CHECK_NEAR(at90.thrust_n, 22.0, 1.0e-9);
        CHECK_NEAR(at95.thrust_n, 9.1, 1.0e-9);
        std::printf("    22 N needs %.3g W, 9.1 N needs %.3g W\n", p90, p95);
        CHECK(p90 >= 1.6e6 && p90 <= 2.0e6);
        CHECK(p95 >= 7.0e5 - 1.0 && p95 <= 8.0e5);  // 700 kW to the rounding of 9.1 / 13e-6
    }
    {
        TEST("JAXA concept: the power to cancel the drag at 180 and 140 km, against the 470 W and 3.3 kW that Romano et al. quote");
        // The case is "within a factor of 2" (docs/VALIDATION.md, case 28), fixed before running. It is NOT met: the model needs
        // several times the quoted power. The study's atmosphere, C_D and solar activity are unknown (its paper is unread), so
        // the gap is recorded as an open finding, not explained. These checks keep the gap from changing unnoticed; they do not
        // say it is right.
        std::vector<abep::DensityTable::Node> nodes;
        CHECK(readPaperDensity(std::string(ABEP_DATA_DIR) + "/romano2018_curves.csv", nodes));
        if (!nodes.empty()) {
            const abep::DensityTable paper(nodes);
            const double inclination = units::deg_to_rad(96.2);  // polar sun-synchronous
            struct Quoted {
                double km;
                double watts;
            };
            for (const Quoted q : {Quoted{180.0, 470.0}, Quoted{140.0, 3300.0}}) {
                const double drag = dragNewtons(q.km * 1.0e3, inclination, paper.density(q.km * 1.0e3), 1.5);
                const double power_at_14 = drag / units::mn_per_kw_to_n_per_w(14.0);  // the least the quoted thrust to power needs
                const double power_at_10 = drag / units::mn_per_kw_to_n_per_w(10.0);
                std::printf("    %.0f km: drag %.1f mN, power %.0f to %.0f W for 14 to 10 mN/kW; quoted %.0f W (model %.1f to %.1f times)\n",
                            q.km, drag * 1.0e3, power_at_14, power_at_10, q.watts, power_at_14 / q.watts, power_at_10 / q.watts);
                // After the fact, and not part of the case: the drag on the study's 0.48 m^2 inlet area with C_D = 1, at 14 mN/kW,
                // comes to about the quoted power at both altitudes (finding 23). A fit to two numbers, not an explanation.
                const double fit = dragNewtons(q.km * 1.0e3, inclination, paper.density(q.km * 1.0e3), 0.48, 1.0) /
                                   units::mn_per_kw_to_n_per_w(14.0);
                std::printf("      the inlet area 0.48 m^2 with C_D = 1 would give %.0f W at 14 mN/kW (%.2f of the quoted)\n", fit,
                            fit / q.watts);
                CHECK(drag > 0.0);
                const bool within_factor_2 = q.watts >= power_at_14 / 2.0 && q.watts <= power_at_10 * 2.0;
                CHECK(!within_factor_2);                             // the documented gap
                CHECK(power_at_14 / q.watts > 3.0 && power_at_14 / q.watts < 30.0);
            }
        }
    }
}
