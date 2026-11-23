// abep_run: run one mission and write its results as CSV.
//
//   abep_run [name=value ...] [out=DIRECTORY]
//   abep_run --help
//
// It describes the run with the settings of include/abep/run_config.hpp (altitude_km=300 mass_kg=150 ...), runs it in the 1976
// standard atmosphere, finds the time to decay with the thruster off and the thruster power that holds the altitude, and writes
// samples.csv, orbits.csv and summary.csv into DIRECTORY (default abep_out). The columns are in docs/OUTPUT.md. The defaults are
// illustrative, not data about any real satellite or thruster.
//
// On Windows, started by a double-click in Explorer, the program has a console window of its own that closes the moment it exits, so
// the summary would flash by: it then says what it is and waits for Enter before it closes. From a command prompt or a script it
// exits at once (ABEP_PAUSE=1 forces the wait, ABEP_PAUSE=0 turns it off).

#include <cstddef>
#include <cstdio>
#include <cstdlib>
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

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

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

// The first character of an environment variable, or 0 if it is not set.
char first_character_of(const char* name) {
#ifdef _WIN32
    char buffer[8] = {0};
    const DWORD length = GetEnvironmentVariableA(name, buffer, sizeof buffer);
    return length > 0 && length < sizeof buffer ? buffer[0] : '\0';
#else
    const char* value = std::getenv(name);
    return value != nullptr ? value[0] : '\0';
#endif
}

#ifdef _WIN32
// Whether a standard handle is a console, and not a file or a pipe.
bool is_console(DWORD standard_handle) {
    DWORD mode = 0;
    return GetConsoleMode(GetStdHandle(standard_handle), &mode) != 0;
}
#endif

// Whether to wait for Enter before exiting, so that what was printed can be read. On Windows that is when the program was started
// by a double-click in Explorer: no other program is attached to its console, and its input and output are that console, which is a
// window of its own that closes when the program exits. From a command prompt or a script the console is shared or the output is
// redirected, and the answer is no. ABEP_PAUSE=1 forces it, and ABEP_PAUSE=0 turns it off.
bool should_wait_for_enter() {
    const char forced = first_character_of("ABEP_PAUSE");
    if (forced == '1') return true;
    if (forced == '0') return false;
#ifdef _WIN32
    DWORD attached[2];
    return GetConsoleProcessList(attached, 2) == 1 && is_console(STD_INPUT_HANDLE) && is_console(STD_OUTPUT_HANDLE);
#else
    return false;
#endif
}

void wait_for_enter() {
    std::printf(
        "\n"
        "abep_run is a command-line program. It has just run one mission with the settings it was given (the defaults, if none)\n"
        "and written its three CSV files into the folder named above. In the Windows download, edit settings.txt and double-click\n"
        "\"Simulate and view.bat\" to run your own settings and see the result in the orbit view; \"abep_run.exe --help\", typed in\n"
        "a command prompt, lists every setting.\n"
        "\n"
        "Press Enter to close this window. ");
    std::fflush(stdout);
    (void)std::getchar();
}

}  // namespace

int run(int argc, char** argv) {
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

int main(int argc, char** argv) {
    const bool wait = should_wait_for_enter();
    const int code = run(argc, argv);
    if (wait) wait_for_enter();
    return code;
}
