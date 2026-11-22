#pragma once

// What abep_run does, as calls: build the mission from the settings, run it, find the time to decay and the power to hold the
// altitude, collect the rows of summary.csv, and write the three CSV files. The command-line program and the Python module call
// these same functions, so a run from either gives the same numbers. Described in docs/OUTPUT.md.

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "abep/atmosphere.hpp"
#include "abep/atmosphere_choice.hpp"
#include "abep/csv.hpp"
#include "abep/mission.hpp"
#include "abep/run_config.hpp"
#include "abep/solar_activity_atmosphere.hpp"
#include "abep/standard_atmosphere_1976.hpp"
#include "abep/summary.hpp"
#include "abep/units.hpp"

namespace abep {

struct FullRun {
    Mission mission;
    MissionResult run;
    DecayResult decay;
    bool have_hold = false;  // false if the satellite leaves the density table within one orbit: there is no orbit to hold
    HoldResult hold;
    std::string hold_note;   // why there is no hold result, if there is none
};

// The density table of an atmosphere choice, built on first use and kept for the life of the program.
inline const DensityTable& atmosphere_table(AtmosphereChoice choice) {
    static const DensityTable standard = ussa1976::density_table();
    static const DensityTable low = ecss::density_table(ecss::Activity::Low);
    static const DensityTable moderate = ecss::density_table(ecss::Activity::Moderate);
    static const DensityTable high = ecss::density_table(ecss::Activity::High);
    static const DensityTable high_short_term = ecss::density_table(ecss::Activity::HighShortTerm);
    switch (choice) {
        case AtmosphereChoice::Standard1976: return standard;
        case AtmosphereChoice::EcssLow: return low;
        case AtmosphereChoice::EcssModerate: return moderate;
        case AtmosphereChoice::EcssHigh: return high;
        case AtmosphereChoice::EcssHighShortTerm: return high_short_term;
    }
    return standard;  // not reached
}

inline const char* atmosphere_name(AtmosphereChoice choice) {
    switch (choice) {
        case AtmosphereChoice::Standard1976: return "1976 US Standard Atmosphere";
        case AtmosphereChoice::EcssLow: return ecss::name(ecss::Activity::Low);
        case AtmosphereChoice::EcssModerate: return ecss::name(ecss::Activity::Moderate);
        case AtmosphereChoice::EcssHigh: return ecss::name(ecss::Activity::High);
        case AtmosphereChoice::EcssHighShortTerm: return ecss::name(ecss::Activity::HighShortTerm);
    }
    return "unknown";  // not reached
}

// Throws std::invalid_argument, naming the setting where it can, for settings that make no sense.
inline FullRun simulate(const RunSettings& settings, const DensityTable& atmosphere) {
    if (!(settings.decay_altitude_km >= 0.0)) {
        throw std::invalid_argument("setting 'decay_altitude_km' must be zero (the lower edge of the density table) or more");
    }
    if (!(settings.decay_max_years > 0.0)) throw std::invalid_argument("setting 'decay_max_years' must be positive");
    FullRun out;
    out.mission = make_mission(settings);
    const Satellite& sat = out.mission.satellite;
    const Scenario& sc = out.mission.scenario;
    out.run = run_mission(sat, sc, out.mission.controller, atmosphere);
    out.decay = time_to_decay(sat, sc, atmosphere, units::km_to_m(settings.decay_altitude_km),
                              settings.decay_max_years * kSecondsPerYear);
    try {
        out.hold = power_to_hold_altitude(sat, sc, atmosphere);
        out.have_hold = true;
    } catch (const std::invalid_argument& e) {
        out.hold_note = e.what();  // the inputs were checked by make_mission, so this is the one-orbit case
    }
    return out;
}

// The same, in the atmosphere that the `atmosphere` setting names (0: the 1976 standard, 1 to 4: ECSS-E-ST-10-04C).
inline FullRun simulate(const RunSettings& settings) {
    return simulate(settings, atmosphere_table(atmosphere_choice(settings.atmosphere)));
}

// The rows of summary.csv: the totals of the run, the time to decay, and the power to hold the altitude if there is one.
inline std::vector<KeyValue> summary_rows(const FullRun& r) {
    std::vector<KeyValue> rows = mission_summary(r.run);
    for (const KeyValue& kv : decay_rows(r.decay)) rows.push_back(kv);
    if (r.have_hold) {
        for (const KeyValue& kv : hold_rows(r.hold)) rows.push_back(kv);
    }
    return rows;
}

// Writes samples.csv, orbits.csv and summary.csv into `directory`, which is created if it is not there. Files are written as
// they are, with line feeds only, on every platform. Throws std::runtime_error if one cannot be written.
inline void write_run_files(const FullRun& r, const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    const auto write = [&](const char* name, const std::string& text) {
        const std::filesystem::path path = directory / name;
        std::ofstream file(path, std::ios::binary);
        file << text;
        file.close();
        if (!file) throw std::runtime_error("could not write " + path.string());
    };
    std::ostringstream samples;
    write_samples_csv(samples, r.run.samples);
    write("samples.csv", samples.str());
    std::ostringstream orbits;
    write_orbits_csv(orbits, r.run.orbits);
    write("orbits.csv", orbits.str());
    std::ostringstream summary;
    write_key_values_csv(summary, summary_rows(r));
    write("summary.csv", summary.str());
}

}  // namespace abep
