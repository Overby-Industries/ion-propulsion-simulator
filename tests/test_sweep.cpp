#include "check.hpp"

#include <cmath>
#include <cstddef>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "abep/csv.hpp"
#include "abep/full_run.hpp"
#include "abep/run_config.hpp"
#include "abep/standard_atmosphere_1976.hpp"
#include "abep/sweep.hpp"

// The parameter sweeps (include/abep/sweep.hpp, docs/SWEEPS.md): the grid of settings, the order of its points, that every row is
// the single run's own numbers, that the numbers obey the physics they should, and that a bad sweep is refused with the reason.

namespace {

std::vector<std::string> linesOf(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) out.push_back(line);
    return out;
}

std::vector<std::string> fieldsOf(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream in(line);
    std::string field;
    while (std::getline(in, field, ',')) out.push_back(field);
    if (!line.empty() && line.back() == ',') out.emplace_back();  // getline drops an empty last field
    return out;
}

double number(const std::string& text) {
    std::istringstream in(text);
    in.imbue(std::locale::classic());
    double x = 0.0;
    in >> x;
    return x;
}

// A short mission and a short decay run, so that a sweep of a few points is quick.
abep::RunSettings quick() {
    abep::RunSettings s;
    s.duration_days = 0.05;    // 72 samples
    s.decay_max_years = 0.02;  // a week
    return s;
}

// Does parse_sweep_values refuse this, and say which setting it was?
bool refusedNaming(const std::string& name, const std::string& text) {
    try {
        (void)abep::parse_sweep_values(name, text);
    } catch (const std::invalid_argument& e) {
        return std::string(e.what()).find(name) != std::string::npos;
    }
    return false;
}

std::size_t columnOf(const std::vector<std::string>& header, const std::string& name) {
    for (std::size_t i = 0; i < header.size(); ++i) {
        if (header[i] == name) return i;
    }
    return header.size();
}

}  // namespace

void runSweepTests() {
    const abep::DensityTable ussa = abep::ussa1976::density_table();

    {
        TEST("sweep values: a range with both ends in, a list, one number");
        const std::vector<double> range = abep::parse_sweep_values("altitude_km", "160:300:8");
        CHECK_EQ(range.size(), static_cast<std::size_t>(8));
        for (std::size_t i = 0; i < range.size(); ++i) CHECK_EQ(range[i], 160.0 + 20.0 * static_cast<double>(i));  // 140 / 7 is exact
        CHECK_EQ(range.back(), 300.0);
        const std::vector<double> list = abep::parse_sweep_values("thrust_density_mn_per_kw", "10,13.7,14");
        CHECK_EQ(list.size(), static_cast<std::size_t>(3));
        CHECK_EQ(list[1], 13.7);
        const std::vector<double> one = abep::parse_sweep_values("mass_kg", "150");
        CHECK(one.size() == 1 && one[0] == 150.0);
        const std::vector<double> single = abep::parse_sweep_values("mass_kg", "3:7:1");
        CHECK(single.size() == 1 && single[0] == 3.0);  // a count of 1 is the start
        const std::vector<double> down = abep::parse_sweep_values("altitude_km", "300:100:3");
        CHECK(down.size() == 3 && down[0] == 300.0 && down[1] == 200.0 && down[2] == 100.0);
        const std::vector<double> through_zero = abep::parse_sweep_values("raan_deg", "-10:10:5");
        CHECK(through_zero.size() == 5 && through_zero[0] == -10.0 && through_zero[2] == 0.0 && through_zero[4] == 10.0);
        // The last value is the stop, to the last bit, however the steps before it round.
        const std::vector<double> awkward = abep::parse_sweep_values("altitude_km", "0.1:0.7:7");
        CHECK(awkward.size() == 7 && awkward.back() == 0.7 && awkward.front() == 0.1);
        // Where start + (stop - start) is not stop in the last bit (the difference loses the 1), the end is still exactly stop.
        const std::vector<double> extreme = abep::parse_sweep_values("altitude_km", "10000000000000000:1:3");
        CHECK(extreme.size() == 3 && extreme.front() == 1.0e16 && extreme.back() == 1.0);
    }
    {
        TEST("sweep values: what is refused is refused with the setting named");
        for (const char* bad : {"", "1:2", "1:2:3:4", "1:2:0", "1:2:-1", "1:2:2.5", "1:2:abc", "a:2:3", "1,,2", "1,abc", "1,", "1:inf:3",
                                "1:2:100001"}) {
            CHECK(refusedNaming("mass_kg", bad));
        }
        CHECK_THROWS(abep::parse_sweep_axis("not_a_setting", "1,2"), std::invalid_argument);
        CHECK(abep::parse_sweep_axis("mass_kg", "50:150:3").values.size() == 3);
    }
    {
        TEST("a grid: its size, the order of its points (the last axis fastest), and what is refused");
        const std::vector<abep::SweepAxis> axes = {{"altitude_km", {200.0, 250.0}}, {"mass_kg", {50.0, 100.0, 150.0}}};
        CHECK_EQ(abep::sweep_size(axes), static_cast<std::size_t>(6));
        const abep::RunSettings base;
        for (std::size_t i = 0; i < 6; ++i) {
            const abep::RunSettings s = abep::sweep_point(base, axes, i);
            CHECK_EQ(s.altitude_km, i < 3 ? 200.0 : 250.0);
            CHECK_EQ(s.mass_kg, 50.0 * static_cast<double>(i % 3 + 1));
            CHECK_EQ(s.frontal_area_m2, base.frontal_area_m2);  // everything else is the base
            CHECK_EQ(s.thrust_density_mn_per_kw, base.thrust_density_mn_per_kw);
        }
        CHECK_THROWS(abep::sweep_size({}), std::invalid_argument);
        CHECK_THROWS(abep::sweep_size({{"altitude_km", {}}}), std::invalid_argument);
        CHECK_THROWS(abep::sweep_size({{"altitude_km", {1.0}}, {"altitude_km", {2.0}}}), std::invalid_argument);
        // 400 x 400 = 160,000 points is more than a sweep may have.
        const std::vector<double> many(400, 1.0);
        CHECK_THROWS(abep::sweep_size({{"altitude_km", many}, {"mass_kg", many}}), std::invalid_argument);
        CHECK_EQ(abep::sweep_size({{"altitude_km", {1.0, 2.0}}}), static_cast<std::size_t>(2));
    }
    {
        TEST("a sweep as CSV: a header, a row per point, and every row is the single run's own numbers");
        const std::vector<abep::SweepAxis> axes = {{"altitude_km", {200.0, 250.0}}, {"thrust_density_mn_per_kw", {10.0, 14.0}}};
        std::ostringstream out;
        abep::write_sweep_csv(out, quick(), axes, ussa);
        const std::vector<std::string> lines = linesOf(out.str());
        CHECK_EQ(lines.size(), static_cast<std::size_t>(5));
        if (lines.size() == 5) {
            const std::vector<std::string> header = fieldsOf(lines[0]);
            const std::vector<std::string>& keys = abep::sweep_output_keys();
            CHECK_EQ(header.size(), axes.size() + keys.size());
            CHECK(header[0] == "altitude_km" && header[1] == "thrust_density_mn_per_kw" && header[2] == keys[0]);
            CHECK_EQ(out.str().find('\r'), std::string::npos);  // line feeds only
            for (std::size_t i = 0; i < 4; ++i) {
                const abep::RunSettings s = abep::sweep_point(quick(), axes, i);
                const std::vector<abep::KeyValue> summary = abep::summary_rows(abep::simulate(s, ussa));
                const std::vector<std::string> row = fieldsOf(lines[i + 1]);
                CHECK_EQ(row.size(), header.size());
                if (row.size() != header.size()) continue;
                CHECK_EQ(row[0], abep::format_number(s.altitude_km));
                CHECK_EQ(row[1], abep::format_number(s.thrust_density_mn_per_kw));
                for (std::size_t k = 0; k < keys.size(); ++k) {
                    std::string want;  // empty where the run has no such row
                    for (const abep::KeyValue& kv : summary) {
                        if (kv.key == keys[k]) want = kv.value;
                    }
                    CHECK_EQ(row[axes.size() + k], want);
                }
            }
        }
    }
    {
        TEST("the sweep says what the physics says: the hold power is the drag over the thrust density, and falls with altitude");
        // Tolerance fixed before running: 1e-9 relative. With the default intake (2 m^2, 0.46, 20 km/s) the thrust the intake can
        // support is more than twice the drag at every altitude (the ratio does not depend on the density), so the power that holds
        // the orbit is the mean drag over the thrust density, exactly, and 10 and 14 mN/kW differ by exactly 1.4.
        const std::vector<abep::SweepAxis> axes = {{"altitude_km", {160.0, 200.0, 250.0, 300.0}}, {"thrust_density_mn_per_kw", {10.0, 14.0}}};
        std::ostringstream out;
        abep::write_sweep_csv(out, quick(), axes, ussa);
        const std::vector<std::string> lines = linesOf(out.str());
        CHECK_EQ(lines.size(), static_cast<std::size_t>(9));
        if (lines.size() == 9) {
            const std::vector<std::string> header = fieldsOf(lines[0]);
            const std::size_t power = columnOf(header, "hold_power_w");
            const std::size_t drag = columnOf(header, "hold_mean_drag_n");
            const std::size_t status = columnOf(header, "hold_status");
            CHECK(power < header.size() && drag < header.size() && status < header.size());
            if (power < header.size() && drag < header.size() && status < header.size()) {
                double previous = 1.0e300;
                for (std::size_t a = 0; a < 4; ++a) {
                    const std::vector<std::string> low = fieldsOf(lines[1 + 2 * a]);       // 10 mN/kW
                    const std::vector<std::string> high = fieldsOf(lines[1 + 2 * a + 1]);  // 14 mN/kW
                    CHECK(low[status] == "Holds" && high[status] == "Holds");
                    CHECK_NEAR(number(low[power]) / number(high[power]), 1.4, 1.4e-9);
                    CHECK_NEAR(number(low[power]) * 10.0e-6, number(low[drag]), 1.0e-9 * number(low[drag]));
                    CHECK_NEAR(number(high[power]) * 14.0e-6, number(high[drag]), 1.0e-9 * number(high[drag]));
                    CHECK(number(low[power]) < previous);  // the higher, the thinner the air, the less power
                    previous = number(low[power]);
                }
            }
        }
    }
    {
        TEST("a sweep with a point that makes no sense says which, and runs nothing");
        const std::vector<abep::SweepAxis> axes = {{"capture_efficiency", {0.5, 1.5}}};
        std::ostringstream out;
        std::string what;
        try {
            abep::write_sweep_csv(out, quick(), axes, ussa);
        } catch (const std::invalid_argument& e) {
            what = e.what();
        }
        CHECK(what.find("sweep point 2 of 2") != std::string::npos);
        CHECK(what.find("capture_efficiency=1.5") != std::string::npos);
        CHECK(what.find("capture efficiency must be between 0 and 1") != std::string::npos);
        CHECK(out.str().empty());  // checked before the first run, so not even the header was written
        // And a setting that does not exist as a base is refused the same way as it is for abep_run.
        abep::RunSettings base = quick();
        CHECK_THROWS(abep::set_setting(base, "no_such_setting", 1.0), std::invalid_argument);
    }
}
