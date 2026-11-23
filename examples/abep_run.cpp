// abep_run: run one mission and write its results as CSV.
//
//   abep_run [name=value ...] [out=DIRECTORY] [--view]
//   abep_run --ask [name=value ...]
//   abep_run --help
//
// It describes the run with the settings of include/abep/run_config.hpp (altitude_km=300 mass_kg=150 ...), runs it in the atmosphere
// that the `atmosphere` setting names (the 1976 standard by default), finds the time to decay with the thruster off and the thruster
// power that holds the altitude, and writes samples.csv, orbits.csv and summary.csv into DIRECTORY (default abep_out). The columns are
// in docs/OUTPUT.md. The defaults are illustrative, not data about any real satellite or thruster.
//
// --ask asks for the main settings one by one (Enter keeps the value shown; any other setting is typed as name=value) and, when the
// run is done, offers to show it in the orbit view; --view shows it without asking. The orbit view is the program that ABEP_VIEWER
// names, or ABEPOrbitView.exe in the folder of abep_run.exe (as in the Windows download); it is started as `ABEPOrbitView -- DIRECTORY`
// and not waited for.
//
// On Windows, started by a double-click in Explorer, the program has a console window of its own that closes the moment it exits, so
// the summary would flash by: it then asks its questions as --ask does, and waits for Enter before the window closes. From a command
// prompt or a script it neither asks nor waits (ABEP_PAUSE=1 forces the double-click behaviour, ABEP_PAUSE=0 turns it off).

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

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
#else
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace {

// The settings, one to a line as "  name = default   meaning". The release script (deploy_to_itch.ps1) makes settings.txt from exactly
// these lines of `abep_run --help`, so their form must not change.
void list_settings(std::ostream& os) {
    const abep::RunSettings defaults;
    for (const abep::SettingInfo& s : abep::setting_table()) {
        os << "  " << s.key << " = " << abep::format_number(defaults.*(s.field), 6) << "   " << s.meaning << '\n';
    }
}

void usage(std::ostream& os) {
    os << "usage: abep_run [name=value ...] [out=DIRECTORY] [--view]\n"
          "       abep_run --ask [name=value ...]\n"
          "Runs one mission and writes samples.csv, orbits.csv and summary.csv into DIRECTORY (default abep_out).\n"
          "--ask asks for the main settings one by one (Enter keeps the value shown) and then offers to show the run in the orbit view;\n"
          "--view shows it there when the run is done. The orbit view is the program that the environment variable ABEP_VIEWER names,\n"
          "or ABEPOrbitView.exe in the folder of abep_run.exe. Started by a double-click on Windows, abep_run asks its questions as\n"
          "--ask does and waits for Enter before its window closes.\n"
          "The defaults are illustrative, not data about any real satellite or thruster.\n\n"
          "settings (name = default, meaning):\n";
    list_settings(os);
}

bool parse_number(const std::string& text, double& out) {
    std::istringstream in(text);
    in.imbue(std::locale::classic());
    in >> out;
    return !in.fail() && in.peek() == std::char_traits<char>::eof();
}

std::string trimmed(const std::string& text) {
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return std::string();
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

// The value of an environment variable, or an empty string if it is not set.
std::string environment_value(const char* name) {
#ifdef _WIN32
    std::string value(256, '\0');
    DWORD length = GetEnvironmentVariableA(name, value.data(), static_cast<DWORD>(value.size()));
    if (length >= value.size()) {  // it does not fit: `length` is the size it needs, with the end mark
        value.resize(length);
        length = GetEnvironmentVariableA(name, value.data(), static_cast<DWORD>(value.size()));
    }
    value.resize(length < value.size() ? length : 0);
    return value;
#else
    const char* value = std::getenv(name);
    return value != nullptr ? std::string(value) : std::string();
#endif
}

#ifdef _WIN32
// Whether a standard handle is a console, and not a file or a pipe.
bool is_console(DWORD standard_handle) {
    DWORD mode = 0;
    return GetConsoleMode(GetStdHandle(standard_handle), &mode) != 0;
}
#endif

// Whether the program was started by a double-click in Explorer: it then asks its questions, and waits for Enter before it exits, so
// that what was printed can be read. On Windows that is when no other program is attached to its console, and its input and output are
// that console, which is a window of its own that closes when the program exits. From a command prompt or a script the console is
// shared or the input or the output is redirected, and the answer is no. ABEP_PAUSE=1 forces it, and ABEP_PAUSE=0 turns it off.
bool started_by_double_click() {
    const std::string forced = environment_value("ABEP_PAUSE");
    if (!forced.empty() && forced[0] == '1') return true;
    if (!forced.empty() && forced[0] == '0') return false;
#ifdef _WIN32
    DWORD attached[2];
    return GetConsoleProcessList(attached, 2) == 1 && is_console(STD_INPUT_HANDLE) && is_console(STD_OUTPUT_HANDLE);
#else
    return false;
#endif
}

// The last thing a double-clicked program does: say what it is, and wait, so that the window does not close under the reader.
void wait_for_enter(int exit_code, bool viewer_started) {
    std::printf("\n");
    if (exit_code != 0) {
        std::printf("abep_run stopped with an error; the message above says what was wrong.\n\n");
    } else if (viewer_started) {
        std::printf("The orbit view is opening in a window of its own.\n\n");
    } else {
        std::printf(
            "abep_run is a command-line program: it has just run one mission and written its three CSV files into the folder named\n"
            "above. Start it again to try other settings. In the Windows download, settings.txt and \"Simulate and view.bat\" do the\n"
            "same without the questions, and \"abep_run.exe --help\", typed in a command prompt, lists every setting.\n\n");
    }
    std::printf("Press Enter to close this window. ");
    std::fflush(stdout);
    std::string ignored;
    (void)std::getline(std::cin, ignored);
}

// One line of the standard input, without its end and the spaces around it. False, after a line break, at the end of the input. A byte
// order mark at the start of the input (a file of answers saved by an editor, or the pipe of some shells) is not part of the first line.
bool read_line(std::string& line) {
    static bool first_line = true;
    std::fflush(stdout);
    if (!std::getline(std::cin, line)) {
        std::printf("\n");
        return false;
    }
    if (first_line && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
    first_line = false;
    line = trimmed(line);
    return true;
}

// Asks a yes or no question; Enter gives `default_answer`, and the end of the input is a no.
bool ask_yes_no(const char* prompt, bool default_answer) {
    for (;;) {
        std::printf("%s", prompt);
        std::string answer;
        if (!read_line(answer)) return false;
        if (answer.empty()) return default_answer;
        if (answer[0] == 'y' || answer[0] == 'Y') return true;
        if (answer[0] == 'n' || answer[0] == 'N') return false;
        std::printf("  Please answer y or n.\n");
    }
}

double value_of(const abep::RunSettings& settings, const std::string& key) {
    for (const abep::SettingInfo& s : abep::setting_table()) {
        if (key == s.key) return settings.*(s.field);
    }
    throw std::logic_error("there is no setting named '" + key + "'");
}

// Applies one argument of the command line: name=value for a setting, or out=DIRECTORY. Throws std::invalid_argument for what is
// neither, for a value that is not a number and for a setting that does not exist.
void apply_argument(const std::string& arg, abep::RunSettings& settings, std::string& out_dir) {
    const std::size_t eq = arg.find('=');
    if (eq == std::string::npos || eq == 0) throw std::invalid_argument("expected name=value, got '" + arg + "'");
    const std::string key = arg.substr(0, eq);
    const std::string value = arg.substr(eq + 1);
    if (key == "out") {
        out_dir = value;
        return;
    }
    double number = 0.0;
    if (!parse_number(value, number)) throw std::invalid_argument("'" + value + "' is not a number (setting '" + key + "')");
    abep::set_setting(settings, key, number);
}

// Gives a setting the value that `text` is, if that makes a mission that makes sense. If not, says why and changes nothing.
bool try_setting(abep::RunSettings& settings, const std::string& key, const std::string& text) {
    double number = 0.0;
    if (!parse_number(text, number)) {
        std::printf("  '%s' is not a number%s.\n", text.c_str(), text.find(',') != std::string::npos ? " (use a point for decimals)" : "");
        return false;
    }
    abep::RunSettings trial = settings;
    try {
        abep::set_setting(trial, key, number);
        (void)abep::make_mission(trial);
    } catch (const std::exception& e) {
        std::printf("  %s\n", e.what());
        return false;
    }
    settings = trial;
    return true;
}

struct Question {
    const char* key;
    const char* text;
};

// The settings that a first visit wants to change. Every other setting can be typed as name=value after them.
const Question kQuestions[] = {
    {"altitude_km", "Orbit altitude, km"},
    {"mass_kg", "Satellite mass, kg"},
    {"frontal_area_m2", "Frontal area facing the air, m^2"},
    {"thrust_density_mn_per_kw", "Thrust per electrical power, mN/kW"},
    {"atmosphere", "Atmosphere (0 = 1976 standard; 1, 2, 3 = ECSS low, moderate, high solar activity; 4 = high, short term)"},
    {"duration_days", "Length of the run, days"},
};

// Asks for the main settings one by one, and then for any others as name=value. What is answered goes into `settings` and into `given`
// as name=value texts, the way it would be typed on the command line. A wrong answer is said to be wrong and asked again; the end of
// the input stops the questions and leaves the rest as they are.
void ask_settings(abep::RunSettings& settings, std::string& out_dir, std::vector<std::string>& given) {
    std::printf("ABEP: how much power would an air-breathing electric thruster need to hold a satellite's altitude?\n"
                "Answer each question, or press Enter to keep the value in [brackets].\n\n");
    bool input_ended = false;
    for (const Question& q : kQuestions) {
        while (!input_ended) {
            std::printf("%s [%s]: ", q.text, abep::format_number(value_of(settings, q.key), 6).c_str());
            std::string answer;
            if (!read_line(answer)) {
                input_ended = true;
            } else if (answer.empty()) {
                break;
            } else if (try_setting(settings, q.key, answer)) {
                given.push_back(std::string(q.key) + "=" + answer);
                break;
            }
        }
    }
    if (!input_ended) {
        std::printf("\nAny other setting: type name=value (? lists them), one to a line, and an empty line to run the mission.\n");
        for (;;) {
            std::printf("> ");
            std::string line;
            if (!read_line(line) || line.empty()) break;
            if (line == "?") {
                std::printf("settings (name = default, meaning):\n");
                list_settings(std::cout);
                continue;
            }
            const std::size_t eq = line.find('=');
            if (eq == std::string::npos || eq == 0) {
                std::printf("  expected name=value (or ? for the list)\n");
                continue;
            }
            const std::string key = trimmed(line.substr(0, eq));
            const std::string value = trimmed(line.substr(eq + 1));
            if (key == "out") {
                out_dir = value;
                given.push_back("out=" + value);
            } else if (try_setting(settings, key, value)) {
                given.push_back(key + "=" + value);
            }
        }
    }
    std::string shown = "abep_run";
    for (const std::string& argument : given) {
        shown += argument.find(' ') == std::string::npos ? " " + argument : " \"" + argument + "\"";
    }
    std::printf("\nRunning: %s\n\n", shown.c_str());
}

// The orbit view's program: the one that ABEP_VIEWER names, or on Windows ABEPOrbitView.exe in the folder of abep_run.exe. An empty path,
// and in `problem` the reason, if there is none.
std::filesystem::path find_viewer(std::string& problem) {
    std::error_code ignored;
    const std::string named = environment_value("ABEP_VIEWER");
    if (!named.empty()) {
        if (std::filesystem::is_regular_file(named, ignored)) return std::filesystem::path(named);
        problem = "the program that ABEP_VIEWER names does not exist: " + named;
        return std::filesystem::path();
    }
#ifdef _WIN32
    std::wstring own(512, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, own.data(), static_cast<DWORD>(own.size()));
        if (length == 0 || own.size() > 65536) {
            own.clear();
            break;
        }
        if (length < own.size()) {
            own.resize(length);
            break;
        }
        own.resize(own.size() * 2);  // it may have been cut short
    }
    if (!own.empty()) {
        const std::filesystem::path beside = std::filesystem::path(own).parent_path() / L"ABEPOrbitView.exe";
        if (std::filesystem::is_regular_file(beside, ignored)) return beside;
    }
#endif
    problem = "the orbit view was not found: ABEPOrbitView.exe is expected in the folder of abep_run.exe (as in the Windows download), "
              "or the environment variable ABEP_VIEWER can name the program";
    return std::filesystem::path();
}

// Starts the orbit view on a folder of results, as `viewer -- FOLDER`, without waiting for it and without sharing the console with it.
// False if it would not start.
bool start_viewer(const std::filesystem::path& viewer, const std::string& out_dir) {
    std::error_code error;
    std::filesystem::path folder = std::filesystem::absolute(out_dir, error);
    if (error) folder = out_dir;
    folder = folder.lexically_normal();
    if (!folder.has_filename() && folder != folder.root_path()) folder = folder.parent_path();  // no separator at the end
#ifdef _WIN32
    std::wstring command = L"\"" + viewer.native() + L"\" -- \"" + folder.native() + L"\"";
    const std::wstring directory = viewer.parent_path().native();
    STARTUPINFOW startup = {};
    startup.cb = static_cast<DWORD>(sizeof startup);
    PROCESS_INFORMATION process = {};
    if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, directory.empty() ? nullptr : directory.c_str(),
                       &startup, &process) == 0) {
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
#else
    if (access(viewer.c_str(), X_OK) != 0) return false;
    const pid_t child = fork();
    if (child < 0) return false;
    if (child == 0) {
        const int null_device = open("/dev/null", O_RDWR);  // it must not hold on to the caller's input and output
        if (null_device >= 0) {
            (void)dup2(null_device, 0);
            (void)dup2(null_device, 1);
            (void)dup2(null_device, 2);
        }
        (void)setsid();
        execl(viewer.c_str(), viewer.c_str(), "--", folder.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    return true;
#endif
}

// Runs the program. `double_click` says that it was started by a double-click, so that it asks its questions when it was given nothing
// to do, and offers the orbit view when the run is done. `viewer_started` says whether it has started the orbit view.
int run_program(int argc, char** argv, bool double_click, bool& viewer_started) {
    try {
        std::vector<std::string> given;  // the name=value arguments, and then the answers to the questions
        bool ask = false;
        bool view = false;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                usage(std::cout);
                return 0;
            }
            if (arg == "--ask") {
                ask = true;
            } else if (arg == "--view") {
                view = true;
            } else {
                given.push_back(arg);
            }
        }

        abep::RunSettings settings;
        std::string out_dir = "abep_out";
        for (const std::string& arg : given) apply_argument(arg, settings, out_dir);
        if (double_click && given.empty() && !view) ask = true;
        if (ask) {
            (void)abep::make_mission(settings);  // what the command line gave has to make sense before anything is asked about it
            ask_settings(settings, out_dir, given);
        }

        const abep::AtmosphereChoice atmosphere = abep::atmosphere_choice(settings.atmosphere);
        const abep::FullRun result = abep::simulate(settings);
        if (atmosphere != abep::AtmosphereChoice::Standard1976) std::printf("atmosphere: %s\n", abep::atmosphere_name(atmosphere));
        abep::write_run_files(result, out_dir);

        const abep::MissionResult& mission = result.run;
        std::printf("run: %.0f s, ended: %s, %zu orbits, %zu samples, thrust duty %.3f\n", mission.elapsed_s,
                    abep::end_reason_name(mission.end_reason), mission.orbits.size(), mission.samples.size(), mission.thrust_duty());
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

        // The orbit view: started when --view asks for it, or when whoever answered the questions says yes. Nobody is asked about
        // a program that is not there.
        std::string problem;
        const std::filesystem::path viewer = find_viewer(problem);
        if (view && viewer.empty()) {
            std::fprintf(stderr, "abep_run: %s\n", problem.c_str());
            return 2;
        }
        if (view || (!viewer.empty() && (ask || double_click) && ask_yes_no("\nShow this run in the orbit view? [Y/n] ", true))) {
            if (!start_viewer(viewer, out_dir)) {
                std::fprintf(stderr, "abep_run: the orbit view would not start (%s)\n", viewer.string().c_str());
                return 2;
            }
            std::printf("Starting the orbit view...\n");
            viewer_started = true;
        }
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "abep_run: %s\n(abep_run --help lists the settings)\n", e.what());
        return 2;
    }
}

}  // namespace

int main(int argc, char** argv) {
    const bool double_click = started_by_double_click();
    bool viewer_started = false;
    const int code = run_program(argc, argv, double_click, viewer_started);
    if (double_click) wait_for_enter(code, viewer_started);
    return code;
}
