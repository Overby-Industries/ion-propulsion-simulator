// abep_run: run one mission and write its results as CSV.
//
//   abep_run [name=value ...] [out=DIRECTORY]
//   abep_run --help
//
// It describes the run with the settings of include/abep/run_config.hpp (altitude_km=300 mass_kg=150 ...), runs it in the 1976
// standard atmosphere, finds the time to decay with the thruster off and the thruster power that holds the altitude, and writes
// samples.csv, orbits.csv and summary.csv into DIRECTORY (default abep_out). The columns are in docs/OUTPUT.md. The defaults are
// illustrative, not data about any real satellite or thruster.

#include <cstddef>
#include <cstdio>
#include <iostream>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>

#include "abep/csv.hpp"
#include "abep/full_run.hpp"
#include "abep/mission.hpp"
#include "abep/run_config.hpp"
#include "abep/standard_atmosphere_1976.hpp"
#include "abep/units.hpp"

namespace {

void usage(std::ostream& os) {
    os << "usage: abep_run [name=value ...] [out=DIRECTORY]\n"
          "Runs one mission and writes samples.csv, orbits.csv and summary.csv into DIRECTORY (default abep_out).\n"
          "The defaults are illustrative, not data about any real satellite or thruster.\n\n"
          "settings (name = default, meaning):\n";
    const abep::RunSettings defaults;
    for (const abep::SettingInfo& s : abep::setting_table()) {
        os << "  " << s.key << " = " << abep::format_number(defaults.*(s.field), 6) << "   " << s.meaning << '\n';
    }
}

bool parse_number(const std::string& text, double& out) {
    std::istringstream in(text);
    in.imbue(std::locale::classic());
    in >> out;
    return !in.fail() && in.peek() == std::char_traits<char>::eof();
}

}  // namespace

int main(int argc, char** argv) {
    try {
        abep::RunSettings settings;
        std::string out_dir = "abep_out";
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                usage(std::cout);
                return 0;
            }
            const std::size_t eq = arg.find('=');
            if (eq == std::string::npos || eq == 0) throw std::invalid_argument("expected name=value, got '" + arg + "'");
            const std::string key = arg.substr(0, eq);
            const std::string value = arg.substr(eq + 1);
            if (key == "out") {
                out_dir = value;
                continue;
            }
            double number = 0.0;
            if (!parse_number(value, number)) throw std::invalid_argument("'" + value + "' is not a number (setting '" + key + "')");
            abep::set_setting(settings, key, number);
        }

        const abep::AtmosphereChoice atmosphere = abep::atmosphere_choice(settings.atmosphere);
        const abep::FullRun result = abep::simulate(settings);
        if (atmosphere != abep::AtmosphereChoice::Standard1976) std::printf("atmosphere: %s\n", abep::atmosphere_name(atmosphere));
        abep::write_run_files(result, out_dir);

        const abep::MissionResult& run = result.run;
        std::printf("run: %.0f s, ended: %s, %zu orbits, %zu samples, thrust duty %.3f\n", run.elapsed_s,
                    abep::end_reason_name(run.end_reason), run.orbits.size(), run.samples.size(), run.thrust_duty());
        if (result.decay.decayed) {
            std::printf("with the thruster off the satellite is down to %.0f km after %.1f days\n",
                        abep::units::m_to_km(result.decay.final_altitude_m), result.decay.time_s / 86400.0);
        } else {
            std::printf("with the thruster off the satellite is still at %.0f km after %.1f days (the longest simulated)\n",
                        abep::units::m_to_km(result.decay.final_altitude_m), result.decay.time_s / 86400.0);
        }
        if (!result.have_hold) {
            std::printf("power to hold the altitude: not computed (%s)\n", result.hold_note.c_str());
        } else if (result.hold.status == abep::HoldResult::Status::Holds) {
            std::printf("power to hold the altitude: %.3g W of thruster power, which asks %.3g W of array (rated thruster power %.0f W)\n",
                        result.hold.power_w, result.hold.array_power_w, result.mission.satellite.thruster_power_w);
        } else {
            std::printf("power to hold the altitude: none does, the intake supports %.2e N and the drag is %.2e N\n",
                        result.hold.mean_intake_limit_n, result.hold.mean_drag_n);
        }
        std::printf("files written to %s\n", out_dir.c_str());
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "abep_run: %s\n(abep_run --help lists the settings)\n", e.what());
        return 2;
    }
}
