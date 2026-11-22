#include "check.hpp"

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "abep/csv.hpp"
#include "abep/eclipse.hpp"
#include "abep/flight.hpp"
#include "abep/full_run.hpp"
#include "abep/mission.hpp"
#include "abep/orbit.hpp"
#include "abep/run_config.hpp"
#include "abep/standard_atmosphere_1976.hpp"
#include "abep/summary.hpp"
#include "abep/units.hpp"

namespace {

namespace units = abep::units;

const abep::DensityTable& ussa() {
    static const abep::DensityTable table = abep::ussa1976::density_table();
    return table;
}

// An illustrative satellite with a thermal node, so that every column of the output varies.
abep::Satellite satellite() {
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
    s.thermal = abep::ThrusterThermal{5000.0, 5.0, 300.0};
    s.initial_temperature_k = 300.0;
    return s;
}

abep::MissionResult run() {
    abep::Scenario sc;
    sc.initial = abep::circular_orbit_state(250.0e3, units::deg_to_rad(51.6), units::deg_to_rad(30.0), 0.0);
    sc.sun_unit = abep::sun_direction_from_ra_dec(0.0, 0.0);
    sc.duration_s = 17000.0;  // three orbits and a bit
    sc.step_s = 10.0;
    sc.sample_every = 7;
    abep::ControllerConfig cfg;
    cfg.temperature_resume_k = 360.0;
    cfg.temperature_limit_k = 400.0;
    return abep::run_mission(satellite(), sc, cfg, ussa());
}

std::vector<std::string> lines_of(const std::string& text) {
    std::vector<std::string> out;
    std::string line;
    std::istringstream in(text);
    while (std::getline(in, line)) out.push_back(line);
    return out;
}

std::vector<std::string> fields_of(const std::string& line) {
    std::vector<std::string> out;
    std::string field;
    std::istringstream in(line);
    while (std::getline(in, field, ',')) out.push_back(field);
    return out;
}

double number(const std::string& text) {
    std::istringstream in(text);
    in.imbue(std::locale::classic());
    double x = std::numeric_limits<double>::quiet_NaN();
    in >> x;
    return x;
}

// A stream that is set up as hostilely as a stream can be for writing a CSV: a comma for the decimal point, a dot for thousands,
// digit groups of three, a plus sign, scientific notation, three digits.
struct CommaPunct : std::numpunct<char> {
    char do_decimal_point() const override { return ','; }
    char do_thousands_sep() const override { return '.'; }
    std::string do_grouping() const override { return "\3"; }
};

}  // namespace

void runCsvTests() {
    {
        TEST("csv: one row per sample, named columns, and every number reads back as the same double");
        const abep::MissionResult r = run();
        CHECK(r.samples.size() > 100);
        std::ostringstream os;
        abep::write_samples_csv(os, r.samples);
        const std::vector<std::string> rows = lines_of(os.str());
        CHECK_EQ(rows.size(), r.samples.size() + 1);
        CHECK(rows[0] == "t_s,mode,altitude_km,density_kg_m3,relative_speed_m_s,drag_n,thrust_n,net_drag_n,thruster_power_w,"
                         "generation_w,load_w,battery_wh,temperature_k,sunlit");
        long wrong_fields = 0;
        long wrong_numbers = 0;
        long wrong_modes = 0;
        bool seen[abep::kModeCount] = {};
        for (std::size_t i = 0; i < r.samples.size() && i + 1 < rows.size(); ++i) {
            const abep::Sample& s = r.samples[i];
            const std::vector<std::string> f = fields_of(rows[i + 1]);
            if (f.size() != 14) {
                ++wrong_fields;
                continue;
            }
            if (f[1] != abep::mode_name(s.mode)) ++wrong_modes;
            seen[static_cast<std::size_t>(s.mode)] = true;
            const double want[] = {s.t_s, 0.0, units::m_to_km(s.altitude_m), s.density_kg_m3, s.relative_speed_m_s, s.drag_n,
                                   s.thrust_n, s.net_drag_n, s.thruster_power_w, s.generation_w, s.load_w, s.battery_wh,
                                   s.temperature_k, s.sunlit ? 1.0 : 0.0};
            for (std::size_t c = 0; c < 14; ++c) {
                if (c == 1) continue;  // the mode, a name
                if (number(f[c]) != want[c]) ++wrong_numbers;  // exact: 17 digits read back as the same double
            }
        }
        CHECK_EQ(wrong_fields, 0L);
        CHECK_EQ(wrong_modes, 0L);
        CHECK_EQ(wrong_numbers, 0L);
        int modes_seen = 0;
        for (const bool b : seen) modes_seen += b ? 1 : 0;
        CHECK(modes_seen >= 3);  // the run goes through several modes, so the names are tested on more than one
    }
    {
        TEST("csv: one row per orbit, with the duty, and an empty list is just the header");
        const abep::MissionResult r = run();
        CHECK(r.orbits.size() >= 3);
        std::ostringstream os;
        abep::write_orbits_csv(os, r.orbits);
        const std::vector<std::string> rows = lines_of(os.str());
        CHECK_EQ(rows.size(), r.orbits.size() + 1);
        CHECK(rows[0] == "orbit,duration_s,thrust_s,duty,eclipse_s,generated_wh,load_wh,thruster_wh,battery_change_wh,spilled_wh");
        long wrong = 0;
        for (std::size_t i = 0; i < r.orbits.size() && i + 1 < rows.size(); ++i) {
            const abep::OrbitBalance& b = r.orbits[i];
            const std::vector<std::string> f = fields_of(rows[i + 1]);
            const double want[] = {static_cast<double>(i), b.duration_s, b.thrust_s, b.duty(), b.eclipse_s, b.generated_wh,
                                   b.load_wh, b.thruster_wh, b.battery_change_wh, b.spilled_wh};
            if (f.size() != 10) {
                ++wrong;
                continue;
            }
            for (std::size_t c = 0; c < 10; ++c) {
                if (number(f[c]) != want[c]) ++wrong;
            }
        }
        CHECK_EQ(wrong, 0L);
        std::ostringstream none_samples;
        abep::write_samples_csv(none_samples, {});
        CHECK_EQ(lines_of(none_samples.str()).size(), 1u);
        std::ostringstream none_orbits;
        abep::write_orbits_csv(none_orbits, {});
        CHECK_EQ(lines_of(none_orbits.str()).size(), 1u);
    }
    {
        TEST("csv: the same text whatever the stream's locale and settings, and the stream is left as it was");
        const abep::MissionResult r = run();
        std::ostringstream plain;
        abep::write_samples_csv(plain, r.samples);
        abep::write_orbits_csv(plain, r.orbits);

        std::ostringstream hostile;
        hostile.imbue(std::locale(std::locale::classic(), new CommaPunct));
        hostile.precision(3);
        hostile.setf(std::ios_base::showpos | std::ios_base::scientific | std::ios_base::uppercase);
        const std::ios_base::fmtflags flags = hostile.flags();
        hostile << 1234567.5;  // what this stream would do to a number: not what a CSV needs
        CHECK(hostile.str() == "+1,235E+06");
        hostile.str("");
        abep::write_samples_csv(hostile, r.samples);
        abep::write_orbits_csv(hostile, r.orbits);
        CHECK(hostile.str() == plain.str());
        CHECK(hostile.flags() == flags);
        CHECK_EQ(hostile.precision(), std::streamsize(3));
        CHECK(std::use_facet<std::numpunct<char>>(hostile.getloc()).decimal_point() == ',');  // the locale is back
        hostile.str("");
        hostile << 1234567.5;
        CHECK(hostile.str() == "+1,235E+06");  // and so are the settings
    }
    {
        TEST("csv: fewer digits give shorter numbers that read back close, and a number of digits that is not 1 to 17 is refused");
        const abep::MissionResult r = run();
        std::ostringstream full;
        abep::write_samples_csv(full, r.samples);
        std::ostringstream six;
        abep::write_samples_csv(six, r.samples, 6);
        CHECK(six.str().size() < full.str().size());
        const std::vector<std::string> rows = lines_of(six.str());
        const std::vector<std::string> f = fields_of(rows.size() > 5 ? rows[5] : "");
        CHECK_EQ(f.size(), 14u);
        if (f.size() == 14 && rows.size() > 5) {
            const abep::Sample& s = r.samples[4];
            CHECK(std::fabs(number(f[5]) - s.drag_n) <= 1.0e-5 * std::fabs(s.drag_n));
            CHECK(number(f[5]) != s.drag_n);  // it was rounded
        }
        CHECK(abep::format_number(0.1) == "0.10000000000000001");
        CHECK(abep::format_number(0.5, 3) == "0.5");
        CHECK(abep::format_number(1.0e-12, 6) == "1e-12");
        CHECK(abep::format_number(-2.0, 17) == "-2");
        CHECK(abep::format_number(std::numeric_limits<double>::infinity()).find("inf") != std::string::npos);
        CHECK_THROWS(abep::format_number(1.0, 0), std::invalid_argument);
        CHECK_THROWS(abep::format_number(1.0, 18), std::invalid_argument);
        std::ostringstream os;
        CHECK_THROWS(abep::write_samples_csv(os, r.samples, 0), std::invalid_argument);
        CHECK_THROWS(abep::write_orbits_csv(os, r.orbits, 18), std::invalid_argument);
        CHECK(os.str().empty());  // refused before anything was written
    }
    {
        TEST("csv: the summary is key,value rows, with the totals of the run and the two summary outputs");
        const abep::MissionResult r = run();
        const std::vector<abep::KeyValue> rows = abep::mission_summary(r);
        const auto value_of = [&](const std::vector<abep::KeyValue>& v, const std::string& key) {
            for (const abep::KeyValue& kv : v) {
                if (kv.key == key) return kv.value;
            }
            return std::string("(missing)");
        };
        CHECK_EQ(number(value_of(rows, "elapsed_s")), r.elapsed_s);
        CHECK(value_of(rows, "end_reason") == "Completed");
        CHECK(value_of(rows, "orbits") == std::to_string(r.orbits.size()));
        CHECK_EQ(number(value_of(rows, "thrust_duty")), r.thrust_duty());
        for (std::size_t m = 0; m < abep::kModeCount; ++m) {
            const std::string key = std::string("seconds_in_") + abep::mode_name(static_cast<abep::Mode>(m));
            CHECK_EQ(number(value_of(rows, key)), r.seconds_in_mode[m]);
        }
        CHECK_EQ(number(value_of(rows, "final_battery_wh")), r.final_battery_wh);
        CHECK_EQ(number(value_of(rows, "final_temperature_k")), r.final_temperature_k);
        CHECK(number(value_of(rows, "final_altitude_km")) > 200.0);

        const abep::DecayResult d{true, 1.5 * 86400.0, 99999.0};
        const std::vector<abep::KeyValue> drows = abep::decay_rows(d);
        CHECK(value_of(drows, "decayed") == "1");
        CHECK_EQ(number(value_of(drows, "decay_time_s")), 1.5 * 86400.0);
        CHECK_EQ(number(value_of(drows, "decay_time_days")), 1.5);
        CHECK_EQ(number(value_of(drows, "decay_final_altitude_km")), units::m_to_km(99999.0));

        abep::HoldResult h;
        h.status = abep::HoldResult::Status::Holds;
        h.power_w = 321.5;
        h.mean_drag_n = 4.0e-3;
        h.mean_intake_limit_n = 8.0e-3;
        h.sunlit_fraction = 0.6;
        h.array_power_w = 900.0;
        const std::vector<abep::KeyValue> hrows = abep::hold_rows(h);
        CHECK(value_of(hrows, "hold_status") == "Holds");
        CHECK_EQ(number(value_of(hrows, "hold_power_w")), 321.5);
        CHECK_EQ(number(value_of(hrows, "hold_mean_drag_n")), 4.0e-3);
        CHECK_EQ(number(value_of(hrows, "hold_mean_intake_limit_n")), 8.0e-3);
        CHECK_EQ(number(value_of(hrows, "hold_sunlit_fraction")), 0.6);
        CHECK_EQ(number(value_of(hrows, "hold_array_power_w")), 900.0);

        std::vector<abep::KeyValue> all = rows;
        all.insert(all.end(), drows.begin(), drows.end());
        all.insert(all.end(), hrows.begin(), hrows.end());
        std::ostringstream os;
        abep::write_key_values_csv(os, all);
        const std::vector<std::string> out = lines_of(os.str());
        CHECK_EQ(out.size(), all.size() + 1);
        CHECK(out[0] == "key,value");
        long not_two_fields = 0;
        for (const std::string& line : out) {
            if (fields_of(line).size() != 2) ++not_two_fields;
        }
        CHECK_EQ(not_two_fields, 0L);

        // A value that would break the file is refused rather than written.
        std::ostringstream bad;
        CHECK_THROWS(abep::write_key_values_csv(bad, {{"key", "a,b"}}), std::invalid_argument);
        CHECK_THROWS(abep::write_key_values_csv(bad, {{"k\ney", "1"}}), std::invalid_argument);
        CHECK_THROWS(abep::write_key_values_csv(bad, {{"key", "say \"hi\""}}), std::invalid_argument);
        CHECK(bad.str().empty());
    }
    {
        TEST("full run: one call gives what the separate calls give, and the files it writes are the writers' text");
        abep::RunSettings s;
        s.duration_days = 0.1;
        s.sample_every = 3.0;
        const abep::FullRun r = abep::simulate(s, ussa());
        const abep::Mission m = abep::make_mission(s);
        const abep::MissionResult direct = abep::run_mission(m.satellite, m.scenario, m.controller, ussa());
        CHECK_EQ(r.run.samples.size(), direct.samples.size());
        CHECK_EQ(r.run.elapsed_s, direct.elapsed_s);
        CHECK(r.have_hold);
        CHECK(r.hold_note.empty());
        CHECK(r.decay.decayed);
        const abep::DecayResult decay = abep::time_to_decay(m.satellite, m.scenario, ussa(), 0.0, 10.0 * abep::kSecondsPerYear);
        CHECK_EQ(r.decay.time_s, decay.time_s);
        // The summary rows are the run's, then the decay's, then the hold's.
        const std::vector<abep::KeyValue> rows = abep::summary_rows(r);
        CHECK_EQ(rows.size(), abep::mission_summary(direct).size() + abep::decay_rows(decay).size() + abep::hold_rows(r.hold).size());
        CHECK(rows.front().key == "elapsed_s");
        CHECK(rows.back().key == "hold_array_power_w");

        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "abep_test_full_run";
        fs::remove_all(dir);
        abep::write_run_files(r, dir / "nested" / "run");  // the folders are made
        const auto slurp = [](const fs::path& p) {
            std::ifstream in(p, std::ios::binary);
            std::ostringstream text;
            text << in.rdbuf();
            return text.str();
        };
        std::ostringstream samples;
        abep::write_samples_csv(samples, r.run.samples);
        std::ostringstream orbits;
        abep::write_orbits_csv(orbits, r.run.orbits);
        std::ostringstream summary;
        abep::write_key_values_csv(summary, rows);
        CHECK(slurp(dir / "nested" / "run" / "samples.csv") == samples.str());
        CHECK(slurp(dir / "nested" / "run" / "orbits.csv") == orbits.str());
        CHECK(slurp(dir / "nested" / "run" / "summary.csv") == summary.str());
        CHECK(slurp(dir / "nested" / "run" / "samples.csv").find('\r') == std::string::npos);  // line feeds only
        // A directory that cannot be made is an error, not a silent skip.
        CHECK_THROWS(abep::write_run_files(r, dir / "nested" / "run" / "samples.csv" / "inside"), std::exception);
        fs::remove_all(dir);

        // A satellite that is out of the density table has no orbit to hold: the run says so and goes on.
        abep::RunSettings low;
        low.altitude_km = 90.0;
        low.duration_days = 0.01;
        const abep::FullRun gone = abep::simulate(low, ussa());
        CHECK(!gone.have_hold);
        CHECK(!gone.hold_note.empty());
        CHECK(gone.run.end_reason == abep::EndReason::AltitudeOutOfRange);
        CHECK(abep::summary_rows(gone).back().key != "hold_array_power_w");

        // Settings of the summaries that make no sense are refused, with the setting named.
        abep::RunSettings bad = s;
        bad.decay_altitude_km = -1.0;
        CHECK_THROWS(abep::simulate(bad, ussa()), std::invalid_argument);
        bad = s;
        bad.decay_max_years = 0.0;
        CHECK_THROWS(abep::simulate(bad, ussa()), std::invalid_argument);
        bad = s;
        bad.mass_kg = -1.0;
        CHECK_THROWS(abep::simulate(bad, ussa()), std::invalid_argument);
    }
}
