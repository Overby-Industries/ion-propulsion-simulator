#include "check.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "abep/atmosphere.hpp"
#include "abep/atmosphere_choice.hpp"
#include "abep/csv.hpp"
#include "abep/drag.hpp"
#include "abep/full_run.hpp"
#include "abep/mission.hpp"
#include "abep/orbit.hpp"
#include "abep/run_config.hpp"
#include "abep/solar_activity_atmosphere.hpp"
#include "abep/standard_atmosphere_1976.hpp"
#include "abep/sweep.hpp"
#include "abep/units.hpp"

#ifndef ABEP_DATA_DIR
#error "ABEP_DATA_DIR must name the repository's data directory; CMakeLists.txt sets it"
#endif

// Validation cases 30 to 35 (docs/VALIDATION.md): the four solar-activity atmospheres of ECSS-E-ST-10-04C Annex G, and the
// atmosphere setting that selects them. The tolerances were written in docs/VALIDATION.md before this file existed; where a
// test only reports a number that the case predicted, it says so.

namespace {

namespace units = abep::units;
namespace ecss = abep::ecss;

constexpr double kCd = 2.2;  // Romano et al.'s drag coefficient, also used for GOCE (its own is not stated in the pages read)

const std::string& dataDir() {
    static const std::string dir = ABEP_DATA_DIR;
    return dir;
}

// The numbers of every line of a data file that is not blank or a comment.
bool readNumbers(const std::string& path, std::vector<std::vector<double>>& rows) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        if (line.find_first_not_of(" \t\r") == std::string::npos || line[0] == '#') continue;
        std::istringstream fields(line);
        std::vector<double> row;
        double x = 0.0;
        while (fields >> x) row.push_back(x);
        rows.push_back(row);
    }
    return !rows.empty();
}

// The paper's atmosphere (Romano et al., Fig. 3, NRLMSISE-00, F10.7 = 140, Ap = 15): the first two columns of the data file.
bool readPaperDensity(std::vector<abep::DensityTable::Node>& nodes) {
    std::ifstream in(dataDir() + "/romano2018_curves.csv");
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

// The drag force, N, on a satellite of 1 kg in a circular orbit of the given altitude (m) and inclination (rad).
double dragNewtons(double altitude_m, double inclination_rad, double density, double area_m2) {
    const abep::State s = abep::circular_orbit_state(altitude_m, inclination_rad);
    return abep::norm(abep::drag_acceleration(s, density, kCd, area_m2, 1.0, true));
}

const abep::DensityTable& choiceTable(int code) { return abep::atmosphere_table(abep::atmosphere_choice(static_cast<double>(code))); }

// The decay rate of GOCE near 280 km, m/day, in the given atmosphere: the slope of the mean radius of each orbit against time over
// three days, with the thruster off. The same measurement as case 25 in test_published.cpp.
double goceDecayRate(const abep::DensityTable& atmosphere) {
    abep::RunSettings s;
    s.altitude_km = 280.0;
    s.inclination_deg = 96.7;
    s.mass_kg = 1050.0;
    s.frontal_area_m2 = 1.1;
    s.drag_coefficient = kCd;
    s.thruster_enabled = 0.0;
    s.duration_days = 3.0;
    s.sample_every = 10.0;
    const abep::Mission m = abep::make_mission(s);
    const abep::MissionResult run = abep::run_mission(m.satellite, m.scenario, m.controller, atmosphere);
    CHECK(run.end_reason == abep::EndReason::Completed);
    const double period = abep::orbital_period_two_body(abep::semi_major_axis(m.scenario.initial));
    const std::size_t orbits = static_cast<std::size_t>(m.scenario.duration_s / period);
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
        const double x = (static_cast<double>(k) + 0.5) * period;
        const double y = sum[k] / static_cast<double>(std::max(count[k], 1));
        sx += x;
        sy += y;
        sxx += x * x;
        sxy += x * y;
    }
    const double n = static_cast<double>(orbits);
    return -86400.0 * (n * sxy - sx * sy) / (n * sxx - sx * sx);
}

// A run for the hold power: an intake that cannot limit, so that the hold power is the mean drag over the thrust density.
abep::RunSettings holdSettings(double altitude_km) {
    abep::RunSettings s;
    s.altitude_km = altitude_km;
    s.inclination_deg = 51.6;
    s.intake_area_m2 = 20.0;
    s.capture_efficiency = 1.0;
    s.exhaust_velocity_km_s = 40.0;
    s.duration_days = 0.1;
    s.decay_max_years = 0.02;
    return s;
}

// The summary rows and the samples of a run as one text, to compare two runs character for character.
std::string runText(const abep::FullRun& r) {
    std::ostringstream text;
    for (const abep::KeyValue& kv : abep::summary_rows(r)) text << kv.key << '=' << kv.value << '\n';
    abep::write_samples_csv(text, r.run.samples);
    return text.str();
}

}  // namespace

void runSolarActivityTests() {
    const abep::DensityTable& ussa = abep::atmosphere_table(abep::AtmosphereChoice::Standard1976);

    {
        TEST("ECSS Table G-4: the data file, the header and the printed values agree, and the table has the shape the standard gives");
        std::vector<std::vector<double>> rows;
        CHECK(readNumbers(dataDir() + "/ecss_e_st_10_04c_table_g4.txt", rows));
        CHECK_EQ(rows.size(), std::size_t{41});
        CHECK_EQ(ecss::kRowCount, std::size_t{41});
        int disagreements = 0;
        for (std::size_t i = 0; i < std::min(rows.size(), ecss::kRowCount); ++i) {
            if (rows[i].size() != 5) {
                ++disagreements;
                continue;
            }
            if (rows[i][0] * 1.0e3 != ecss::kRows[i].altitude_m) ++disagreements;
            if (ecss::kRows[i].altitude_m != 100.0e3 + 20.0e3 * static_cast<double>(i)) ++disagreements;
            for (std::size_t c = 0; c < ecss::kActivityCount; ++c) {
                if (std::fabs(rows[i][1 + c] / ecss::kRows[i].density_kg_m3[c] - 1.0) > 1.0e-15) ++disagreements;
            }
        }
        CHECK_EQ(disagreements, 0);
        // Each column falls with altitude; from 120 km up the four are ordered low < moderate < high, long term < high, short term.
        int not_falling = 0;
        int not_ordered = 0;
        for (std::size_t i = 1; i < ecss::kRowCount; ++i) {
            for (std::size_t c = 0; c < ecss::kActivityCount; ++c) {
                if (!(ecss::kRows[i].density_kg_m3[c] < ecss::kRows[i - 1].density_kg_m3[c])) ++not_falling;
            }
        }
        for (std::size_t i = 0; i < ecss::kRowCount; ++i) {
            const double* d = ecss::kRows[i].density_kg_m3;
            if (ecss::kRows[i].altitude_m >= 120.0e3 && !(d[0] < d[1] && d[1] < d[2] && d[2] < d[3])) ++not_ordered;
        }
        CHECK_EQ(not_falling, 0);
        CHECK_EQ(not_ordered, 0);
        // Eight values typed apart from the data file, from the printed pages (138 and 139), one per level and then again.
        struct Spot {
            double km;
            ecss::Activity level;
            double rho;
        };
        const Spot spots[] = {{100.0, ecss::Activity::Low, 5.31e-7},           {140.0, ecss::Activity::Moderate, 3.98e-9},
                              {200.0, ecss::Activity::High, 4.10e-10},         {260.0, ecss::Activity::HighShortTerm, 1.21e-10},
                              {300.0, ecss::Activity::Low, 5.86e-12},          {400.0, ecss::Activity::Moderate, 4.22e-12},
                              {600.0, ecss::Activity::High, 9.82e-13},         {900.0, ecss::Activity::HighShortTerm, 1.35e-13}};
        for (const Spot& s : spots) {
            const double rho = ecss::density_table(s.level).density(s.km * 1.0e3);
            CHECK(std::fabs(rho / s.rho - 1.0) < 1.0e-12);
        }
        // The table ends where the standard's does.
        const abep::DensityTable moderate = ecss::density_table(ecss::Activity::Moderate);
        CHECK_EQ(moderate.min_altitude_m(), 100.0e3);
        CHECK_EQ(moderate.max_altitude_m(), 900.0e3);
        CHECK_THROWS(moderate.density(99.9e3), std::out_of_range);
        CHECK_THROWS(moderate.density(900.1e3), std::out_of_range);
        CHECK_THROWS(moderate.density(std::numeric_limits<double>::quiet_NaN()), std::out_of_range);
    }
    {
        TEST("interpolation: the 20 km rows of the standard thinned from the 1976 table, recovered with log-linear and with cubic steps");
        // The truth between nodes is the 1976 standard's own rows, 1 km apart to 300 km, 2 km to 500 km, 5 km above; the nodes are
        // its rows 20 km apart. The tolerances are set by what the application can stand (a fifth of the 10% to 15% uncertainty
        // the ECSS standard gives JB-2006), not by what passes: 1% from 200 km, 3% from 150 to 200 km, 10% from 100 to 150 km.
        std::vector<abep::DensityTable::Node> nodes;
        for (const abep::DensityTable::Node& n : abep::ussa1976::kRows) {
            if (n.altitude_m <= 900.0e3 && std::fmod(n.altitude_m, 20.0e3) == 0.0) nodes.push_back(n);
        }
        CHECK_EQ(nodes.size(), std::size_t{41});
        const abep::DensityTable linear(nodes);
        const abep::DensityTable cubic(nodes, abep::DensityTable::Interpolation::MonotoneCubic);
        CHECK(linear.interpolation() == abep::DensityTable::Interpolation::LogLinear);
        CHECK(cubic.interpolation() == abep::DensityTable::Interpolation::MonotoneCubic);
        double worst_linear[3] = {0.0, 0.0, 0.0};
        double worst_cubic[3] = {0.0, 0.0, 0.0};
        for (const abep::DensityTable::Node& n : abep::ussa1976::kRows) {
            if (n.altitude_m > 900.0e3 || std::fmod(n.altitude_m, 20.0e3) == 0.0) continue;
            const std::size_t band = n.altitude_m < 150.0e3 ? 0 : n.altitude_m < 200.0e3 ? 1 : 2;
            worst_linear[band] = std::max(worst_linear[band], std::fabs(linear.density(n.altitude_m) / n.density_kg_m3 - 1.0));
            worst_cubic[band] = std::max(worst_cubic[band], std::fabs(cubic.density(n.altitude_m) / n.density_kg_m3 - 1.0));
        }
        std::printf("    worst error, 100-150 / 150-200 / 200-900 km: log-linear %.2f%% %.2f%% %.2f%%, cubic %.2f%% %.2f%% %.2f%%\n",
                    100.0 * worst_linear[0], 100.0 * worst_linear[1], 100.0 * worst_linear[2], 100.0 * worst_cubic[0],
                    100.0 * worst_cubic[1], 100.0 * worst_cubic[2]);
        CHECK(worst_cubic[0] <= 0.10);
        CHECK(worst_cubic[1] <= 0.03);
        CHECK(worst_cubic[2] <= 0.01);
        CHECK(worst_linear[1] > 0.03);  // why the 20 km tables are not interpolated log-linearly: it misses the 3% band
    }
    {
        TEST("interpolation: the cubic returns its nodes, is exact for an exponential atmosphere, falls where the nodes fall, and does not overshoot");
        // The nodes of each ECSS profile come back as they were given, and the profile falls everywhere between them.
        int changed_nodes = 0;
        int rising = 0;
        for (int code = 1; code <= 4; ++code) {
            const abep::DensityTable& t = choiceTable(code);
            CHECK(t.interpolation() == abep::DensityTable::Interpolation::MonotoneCubic);  // the 20 km rows are not interpolated log-linearly
            for (std::size_t i = 0; i < ecss::kRowCount; ++i) {
                if (t.density(ecss::kRows[i].altitude_m) != ecss::kRows[i].density_kg_m3[code - 1]) ++changed_nodes;
            }
            double previous = t.density(100.0e3);
            for (double km = 100.5; km <= 900.0; km += 0.5) {
                const double now = t.density(km * 1.0e3);
                if (!(now < previous)) ++rising;
                previous = now;
            }
        }
        CHECK_EQ(changed_nodes, 0);
        CHECK_EQ(rising, 0);
        // An exact exponential atmosphere on nodes of uneven spacing: ln(density) is linear, and the cubic reproduces it.
        const double scale_height_m = 37.0e3;
        std::vector<abep::DensityTable::Node> expo;
        for (const double km : {100.0, 117.0, 140.0, 200.0, 230.0, 360.0}) {
            expo.push_back({km * 1.0e3, 1.0e-6 * std::exp(-(km * 1.0e3 - 100.0e3) / scale_height_m)});
        }
        const abep::DensityTable cubic_expo(expo, abep::DensityTable::Interpolation::MonotoneCubic);
        double worst = 0.0;
        for (double km = 100.0; km <= 360.0; km += 0.37) {
            const double exact = 1.0e-6 * std::exp(-(km * 1.0e3 - 100.0e3) / scale_height_m);
            worst = std::max(worst, std::fabs(cubic_expo.density(km * 1.0e3) / exact - 1.0));
        }
        CHECK(worst < 1.0e-12);
        // Nodes that rise and then fall: between the nodes the curve stays inside the nodes' range, with no overshoot.
        const std::vector<abep::DensityTable::Node> bump = {{0.0, 1.0}, {1.0e3, 2.0}, {2.0e3, 1.5}, {3.0e3, 1.4}};
        const abep::DensityTable cubic_bump(bump, abep::DensityTable::Interpolation::MonotoneCubic);
        int outside = 0;
        for (double m = 0.0; m <= 3.0e3; m += 10.0) {
            const double d = cubic_bump.density(m);
            if (d > 2.0 * (1.0 + 1.0e-12) || d < 1.0 * (1.0 - 1.0e-12)) ++outside;
            if (m >= 1.0e3 && m <= 2.0e3 && (d > 2.0 * (1.0 + 1.0e-12) || d < 1.5 * (1.0 - 1.0e-12))) ++outside;
        }
        CHECK_EQ(outside, 0);
        // A steep step between nodes that all fall: the cubic must not rise or dip beyond the nodes on either side of an interval (the
        // slopes at the nodes are kept small by the weighted harmonic mean; an arithmetic mean would overshoot here).
        const std::vector<abep::DensityTable::Node> step = {{0.0, 1.0}, {1.0e3, 0.9}, {2.0e3, 1.0e-3}, {3.0e3, 9.0e-4}};
        const abep::DensityTable cubic_step(step, abep::DensityTable::Interpolation::MonotoneCubic);
        int overshoot = 0;
        for (std::size_t k = 0; k + 1 < step.size(); ++k) {
            double previous_step = cubic_step.density(step[k].altitude_m);
            for (double m = step[k].altitude_m + 5.0; m < step[k + 1].altitude_m; m += 5.0) {
                const double d = cubic_step.density(m);
                if (d > previous_step || d > step[k].density_kg_m3 || d < step[k + 1].density_kg_m3) ++overshoot;
                previous_step = d;
            }
        }
        CHECK_EQ(overshoot, 0);
        // Two nodes are a straight line in ln(density), whichever the interpolation.
        const abep::DensityTable two({{0.0, 1.0}, {10.0, std::exp(-2.0)}}, abep::DensityTable::Interpolation::MonotoneCubic);
        CHECK(std::fabs(two.density(5.0) / std::exp(-1.0) - 1.0) < 1.0e-13);
        // The 1976 standard keeps its log-linear rule.
        CHECK(ussa.interpolation() == abep::DensityTable::Interpolation::LogLinear);
    }
    {
        TEST("interpolation: the cubic's slopes at the nodes are the weighted harmonic mean inside and the three-point formula at the ends");
        // Three nodes 1 km apart with ln(density) = 0, -0.1 and -0.35: secants of -1e-4 and -2.5e-4 per metre. By hand, the slope at the
        // first node is ((2 w0 + w1) s0 - w0 s1) / (w0 + w1) = -2.5e-5, in the middle (w1 + w2) / (w1 / s0 + w2 / s1) = -1/7000, and at
        // the last node -3.25e-4. A slope is the derivative of the cubic at its node, found here by a difference over a millimetre. (The
        // mutation check of this code found that nothing else pinned the end slopes: finding 30.)
        const std::vector<abep::DensityTable::Node> three = {{0.0, 1.0}, {1000.0, std::exp(-0.1)}, {2000.0, std::exp(-0.35)}};
        const abep::DensityTable cubic_three(three, abep::DensityTable::Interpolation::MonotoneCubic);
        const double eps = 1.0e-3;
        const double first = std::log(cubic_three.density(eps) / cubic_three.density(0.0)) / eps;
        const double middle = std::log(cubic_three.density(1000.0 + eps) / cubic_three.density(1000.0 - eps)) / (2.0 * eps);
        const double last = std::log(cubic_three.density(2000.0) / cubic_three.density(2000.0 - eps)) / eps;
        CHECK_NEAR(first, -2.5e-5, 1.0e-9);
        CHECK_NEAR(middle, -1.0 / 7000.0, 1.0e-9);
        CHECK_NEAR(last, -3.25e-4, 1.0e-9);
    }
    {
        TEST("ECSS: JB-2006 and NRLMSISE-00, the standard's two models, agree to a factor of 2 and vary smoothly from 120 to 600 km");
        // A check on the transcription (a misprinted digit or exponent is a bump in the ratio), not a validation of either model.
        // Above 600 km the standard's own NRLMSISE-00 density differs from the sum of its number densities, by up to 13%.
        std::vector<std::vector<double>> msis;
        CHECK(readNumbers(dataDir() + "/ecss_e_st_10_04c_tables_g1_g3.txt", msis));
        CHECK_EQ(msis.size(), std::size_t{46});
        if (msis.size() == 46) {
            for (std::size_t c = 0; c < 3; ++c) {
                std::vector<double> log_ratio;
                double lowest = 1.0e9;
                double highest = 0.0;
                for (std::size_t i = 0; i < ecss::kRowCount; ++i) {
                    const double km = ecss::kRows[i].altitude_m / 1.0e3;
                    if (km < 120.0 || km > 600.0) continue;
                    const std::vector<double>& row = msis[static_cast<std::size_t>(km / 20.0)];
                    CHECK_EQ(row[0], km);
                    const double ratio = ecss::kRows[i].density_kg_m3[c] / row[2 + 2 * c];
                    lowest = std::min(lowest, ratio);
                    highest = std::max(highest, ratio);
                    log_ratio.push_back(std::log(ratio));
                }
                double largest_second_difference = 0.0;
                for (std::size_t i = 1; i + 1 < log_ratio.size(); ++i) {
                    largest_second_difference =
                        std::max(largest_second_difference, std::fabs(log_ratio[i - 1] - 2.0 * log_ratio[i] + log_ratio[i + 1]));
                }
                std::printf("    case %zu: JB-2006 over NRLMSISE-00 %.2f to %.2f, largest second difference of its logarithm %.3f\n", c + 1,
                            lowest, highest, largest_second_difference);
                CHECK(lowest >= 0.5 && highest <= 2.0);
                CHECK(largest_second_difference < 0.3);
            }
        }
    }
    {
        TEST("the atmosphere setting: 0 to 4 name an atmosphere, anything else is refused naming the setting");
        CHECK(abep::atmosphere_choice(0.0) == abep::AtmosphereChoice::Standard1976);
        CHECK(abep::atmosphere_choice(1.0) == abep::AtmosphereChoice::EcssLow);
        CHECK(abep::atmosphere_choice(2.0) == abep::AtmosphereChoice::EcssModerate);
        CHECK(abep::atmosphere_choice(3.0) == abep::AtmosphereChoice::EcssHigh);
        CHECK(abep::atmosphere_choice(4.0) == abep::AtmosphereChoice::EcssHighShortTerm);
        for (const double bad : {-1.0, 0.5, 5.0, 100.0, std::numeric_limits<double>::quiet_NaN()}) {
            CHECK_THROWS(abep::atmosphere_choice(bad), std::invalid_argument);
        }
        abep::RunSettings s;
        CHECK_EQ(s.atmosphere, 0.0);  // the 1976 standard is the default
        abep::set_setting(s, "atmosphere", 7.0);
        try {
            (void)abep::make_mission(s);
            CHECK(false);  // should have thrown
        } catch (const std::invalid_argument& e) {
            CHECK(std::string(e.what()).find("'atmosphere'") != std::string::npos);
        }
        std::string previous;
        for (int code = 0; code <= 4; ++code) {
            const std::string name = abep::atmosphere_name(abep::atmosphere_choice(static_cast<double>(code)));
            CHECK(name.size() > 10 && name != previous);
            const char* const expected[] = {"1976", "low", "moderate", "high long-term", "high short-term"};
            CHECK(name.find(expected[code]) != std::string::npos);
            previous = name;
            CHECK_EQ(choiceTable(code).min_altitude_m(), 100.0e3);
            CHECK_EQ(choiceTable(code).max_altitude_m(), code == 0 ? 1000.0e3 : 900.0e3);
        }
    }
    {
        TEST("simulate(settings): atmosphere 0 is the old two-argument run, character for character, and 1 to 4 fly their own table");
        abep::RunSettings s = holdSettings(250.0);
        const std::string standard = runText(abep::simulate(s, ussa));
        CHECK(runText(abep::simulate(s)) == standard);
        s.atmosphere = 0.0;
        CHECK(runText(abep::simulate(s)) == standard);
        const ecss::Activity levels[] = {ecss::Activity::Low, ecss::Activity::Moderate, ecss::Activity::High, ecss::Activity::HighShortTerm};
        for (int code = 1; code <= 4; ++code) {
            s.atmosphere = static_cast<double>(code);
            const std::string chosen = runText(abep::simulate(s));
            CHECK(chosen == runText(abep::simulate(s, ecss::density_table(levels[code - 1]))));  // its own table, built apart
            CHECK(chosen != standard);
        }
    }
    {
        TEST("hold power: the ratio of two atmospheres' hold powers is the ratio of their densities at the altitude of the orbit, to 5%");
        // Tolerance written before the run (case 33): the mean drag over one orbit scales with the density, and what differs is the
        // curvature of the density over the orbit's altitude oscillation (a few kilometres), about 1.5%, times three.
        abep::RunSettings s = holdSettings(250.0);
        double power[5] = {0.0, 0.0, 0.0, 0.0, 0.0};
        double mean_altitude_m = 0.0;
        for (int code = 1; code <= 4; ++code) {
            s.atmosphere = static_cast<double>(code);
            const abep::FullRun r = abep::simulate(s);
            CHECK(r.have_hold);
            if (!r.have_hold) continue;
            CHECK(r.hold.status == abep::HoldResult::Status::Holds);
            power[code] = r.hold.power_w;
            if (code == 2) {
                const double period = abep::orbital_period_two_body(abep::semi_major_axis(r.mission.scenario.initial));
                double sum = 0.0;
                int n = 0;
                for (const abep::Sample& sample : r.run.samples) {
                    if (sample.t_s < period) {
                        sum += sample.altitude_m;
                        ++n;
                    }
                }
                CHECK(n > 50);
                mean_altitude_m = sum / static_cast<double>(std::max(n, 1));
            }
        }
        std::printf("    hold power at 250 km: low %.1f W, moderate %.1f W, high %.1f W, high short term %.1f W (mean altitude %.1f km)\n",
                    power[1], power[2], power[3], power[4], mean_altitude_m / 1.0e3);
        for (int code = 2; code <= 4; ++code) {
            const double power_ratio = power[code] / power[code - 1];
            const double density_ratio = choiceTable(code).density(mean_altitude_m) / choiceTable(code - 1).density(mean_altitude_m);
            std::printf("    atmosphere %d over %d: hold power ratio %.4f, density ratio at the orbit %.4f\n", code, code - 1, power_ratio,
                        density_ratio);
            CHECK(std::fabs(power_ratio / density_ratio - 1.0) < 0.05);
        }
    }
    {
        TEST("decay and hold: the time to decay falls and the hold power rises from low to moderate to high to high short term");
        abep::RunSettings s = holdSettings(200.0);
        s.decay_max_years = 0.5;
        double decay_days[5] = {0.0, 0.0, 0.0, 0.0, 0.0};
        double hold_w[5] = {0.0, 0.0, 0.0, 0.0, 0.0};
        for (int code = 1; code <= 4; ++code) {
            s.atmosphere = static_cast<double>(code);
            const abep::FullRun r = abep::simulate(s);
            CHECK(r.decay.decayed);
            CHECK(r.have_hold);
            decay_days[code] = r.decay.time_s / 86400.0;
            hold_w[code] = r.hold.power_w;
        }
        std::printf("    at 200 km, time to decay %.2f, %.2f, %.2f, %.2f days; hold power %.0f, %.0f, %.0f, %.0f W\n", decay_days[1],
                    decay_days[2], decay_days[3], decay_days[4], hold_w[1], hold_w[2], hold_w[3], hold_w[4]);
        for (int code = 2; code <= 4; ++code) {
            CHECK(decay_days[code] < decay_days[code - 1]);
            CHECK(hold_w[code] > hold_w[code - 1]);
        }
    }
    {
        TEST("sweeps: an atmosphere axis gives the rows of the single runs, a bad value is refused before any run, a fixed table cannot be swept");
        abep::RunSettings base = holdSettings(250.0);
        const std::vector<abep::SweepAxis> axes = {abep::parse_sweep_axis("atmosphere", "0:4:5")};
        std::ostringstream out;
        abep::write_sweep_csv(out, base, axes);
        std::vector<std::string> lines;
        {
            std::istringstream in(out.str());
            std::string line;
            while (std::getline(in, line)) lines.push_back(line);
        }
        CHECK_EQ(lines.size(), std::size_t{6});  // the header and five points
        const auto split = [](const std::string& line) {
            std::vector<std::string> fields;
            std::string field;
            std::istringstream in(line);
            while (std::getline(in, field, ',')) fields.push_back(field);
            if (!line.empty() && line.back() == ',') fields.emplace_back();
            return fields;
        };
        if (lines.size() == 6) {
            const std::vector<std::string> header = split(lines[0]);
            CHECK_EQ(header.size(), 1 + abep::sweep_output_keys().size());
            CHECK(header[0] == "atmosphere");
            for (std::size_t point = 0; point < 5; ++point) {
                abep::RunSettings s = base;
                s.atmosphere = static_cast<double>(point);
                const std::vector<abep::KeyValue> single = abep::summary_rows(abep::simulate(s));
                const std::vector<std::string> row = split(lines[point + 1]);
                CHECK_EQ(row.size(), header.size());
                if (row.size() != header.size()) continue;
                CHECK(row[0] == std::to_string(point));
                for (std::size_t k = 1; k < header.size(); ++k) {
                    std::string expected;
                    for (const abep::KeyValue& kv : single) {
                        if (kv.key == header[k]) expected = kv.value;
                    }
                    CHECK(row[k] == expected);
                }
            }
        }
        std::ostringstream fixed;
        CHECK_THROWS(abep::write_sweep_csv(fixed, base, axes, ussa), std::invalid_argument);
        CHECK(fixed.str().empty());
        std::ostringstream refused;
        const std::vector<abep::SweepAxis> bad = {abep::parse_sweep_axis("atmosphere", "1,9")};
        try {
            abep::write_sweep_csv(refused, base, bad);
            CHECK(false);  // should have thrown
        } catch (const std::invalid_argument& e) {
            const std::string message = e.what();
            CHECK(message.find("sweep point 2 of 2") != std::string::npos);
            CHECK(message.find("atmosphere") != std::string::npos);
        }
        CHECK(refused.str().empty());  // nothing was run, not even the header
    }
    {
        TEST("GOCE: ESA's 190 m/day lies between the decay rates of the low and the moderate atmosphere; the low one misses the factor of 2 (finding 25)");
        // Case 34, written before the run: ESA, 6 April 2009, about 190 m/day at about 280 km, in an extended solar minimum, with
        // the solar flux close to the low profile's 65 sfu; 1050 kg, 1.1 m^2, C_D 2.2 as in case 25. By hand from the decay
        // equation the rates would be about 106 (low), 423 (moderate), 731 (high) m/day, and 280 in the standard (case 25).
        double rate[5];
        for (int code = 0; code <= 4; ++code) rate[code] = goceDecayRate(choiceTable(code));
        std::printf("    decay rate at 280 km, m/day: standard %.0f, low %.0f, moderate %.0f, high %.0f, high short term %.0f; ESA 190\n",
                    rate[0], rate[1], rate[2], rate[3], rate[4]);
        CHECK(rate[1] < 190.0 && 190.0 < rate[2]);  // the first half of the case, the bracket: met
        // The second half, written before the run: the low rate within a factor of 2 of ESA's. NOT MET, by 0.5%: ESA's rate is 2.01
        // times the low profile's (finding 25), where the estimate by hand had 1.8. The tolerance is not changed. These checks keep
        // the shortfall from changing unnoticed (between 2 and 3 times); they do not say it is right.
        CHECK(190.0 / rate[1] >= 2.0 && 190.0 / rate[1] < 3.0);
        CHECK(rate[1] < rate[2] && rate[2] < rate[3] && rate[3] < rate[4]);
        std::printf("    ESA's 190 m/day is %.2f times the low rate and %.2f of the moderate\n", 190.0 / rate[1], 190.0 / rate[2]);
    }
    {
        TEST("published design points, with the profiles: ESA's demonstrator, Romano et al.'s density, and the JAXA gap");
        // (a) Romano et al. Sect. 1, the ESA study [32]: 1000 kg, 1 m^2, 200 km sun-synchronous, thrust 2 to 20 mN; inside the range for
        // the low and the moderate atmosphere. Predicted before the run by scaling case 26's 14.8 mN: 8.6, 18.5 and 23.8 mN (the high
        // one is over 20 and is reported, not asserted).
        abep::RunSettings demo;
        demo.altitude_km = 200.0;
        demo.inclination_deg = 96.3;
        demo.mass_kg = 1000.0;
        demo.frontal_area_m2 = 1.0;
        demo.drag_coefficient = kCd;
        demo.thrust_density_mn_per_kw = 20.0;
        demo.exhaust_velocity_km_s = 40.0;
        demo.intake_area_m2 = 20.0;
        demo.capture_efficiency = 1.0;
        demo.duration_days = 0.1;
        demo.decay_max_years = 0.02;
        double drag_mn[5];
        for (int code = 1; code <= 4; ++code) {
            demo.atmosphere = static_cast<double>(code);
            const abep::FullRun r = abep::simulate(demo);
            CHECK(r.have_hold);
            drag_mn[code] = r.have_hold ? r.hold.mean_drag_n * 1.0e3 : 0.0;
        }
        std::printf("    the demonstrator's mean drag at 200 km: low %.1f, moderate %.1f, high %.1f, high short term %.1f mN (published thrust 2 to 20)\n",
                    drag_mn[1], drag_mn[2], drag_mn[3], drag_mn[4]);
        CHECK(drag_mn[1] >= 2.0 && drag_mn[1] <= 20.0);
        CHECK(drag_mn[2] >= 2.0 && drag_mn[2] <= 20.0);
        // (b) Romano et al.'s Fig. 3, at the 14 altitudes of the data file: the moderate JB-2006 density within a factor of 2.
        std::vector<abep::DensityTable::Node> nodes;
        CHECK(readPaperDensity(nodes));
        CHECK_EQ(nodes.size(), std::size_t{14});
        if (!nodes.empty()) {
            const abep::DensityTable paper(nodes);
            const abep::DensityTable& moderate = choiceTable(2);
            double lowest = 1.0e9;
            double highest = 0.0;
            for (const abep::DensityTable::Node& n : nodes) {
                const double ratio = moderate.density(n.altitude_m) / paper.density(n.altitude_m);
                lowest = std::min(lowest, ratio);
                highest = std::max(highest, ratio);
            }
            std::printf("    the moderate JB-2006 density over Romano et al.'s curve, 120 to 250 km: %.2f to %.2f (the 1976 standard: ", lowest, highest);
            double std_lowest = 1.0e9;
            double std_highest = 0.0;
            for (const abep::DensityTable::Node& n : nodes) {
                const double ratio = ussa.density(n.altitude_m) / paper.density(n.altitude_m);
                std_lowest = std::min(std_lowest, ratio);
                std_highest = std::max(std_highest, ratio);
            }
            std::printf("%.2f to %.2f)\n", std_lowest, std_highest);
            CHECK(lowest >= 0.5 && highest <= 2.0);
        }
        // (c) The JAXA concept (case 28): 1.5 m^2, 470 W at 180 km and 3.3 kW at 140 km. With every profile the model's power, at
        // the best thrust density, is still at least 3 times the quoted: the gap of finding 23 is not the atmosphere.
        struct Quoted {
            double km;
            double watts;
        };
        const double inclination = units::deg_to_rad(96.2);
        for (const Quoted q : {Quoted{180.0, 470.0}, Quoted{140.0, 3300.0}}) {
            std::printf("    %.0f km, power at 14 mN/kW over the quoted:", q.km);
            for (int code = 1; code <= 4; ++code) {
                const double drag = dragNewtons(q.km * 1.0e3, inclination, choiceTable(code).density(q.km * 1.0e3), 1.5);
                const double ratio = drag / units::mn_per_kw_to_n_per_w(14.0) / q.watts;
                std::printf(" %.1f", ratio);
                CHECK(ratio >= 3.0);
            }
            std::printf("\n");
        }
    }
}
