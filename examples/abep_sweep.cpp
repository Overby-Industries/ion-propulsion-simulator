// abep_sweep: run one mission over a grid of settings and write one row of results per point, as CSV.
//
//   abep_sweep [name=value ...] [name=start:stop:count ...] [name=v1,v2,... ...] [out=FILE]
//   abep_sweep --help
//
// The settings are those of abep_run (include/abep/run_config.hpp). A setting given one number is fixed for the whole sweep; a
// setting given start:stop:count (count values from start to stop, both ends included) or a list v1,v2,... is swept. The grid is
// every combination, the last swept setting changing fastest. Each point is one run in the 1976 standard atmosphere, as abep_run
// does it, and its row has the swept values and the main results of that run's summary.csv. The rows go to FILE, or to the standard
// output without out=. The columns are in docs/SWEEPS.md. The defaults are illustrative, not data about any real satellite or
// thruster.

#include <cstddef>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "abep/csv.hpp"
#include "abep/run_config.hpp"
#include "abep/standard_atmosphere_1976.hpp"
#include "abep/sweep.hpp"

namespace {

void usage(std::ostream& os) {
    os << "usage: abep_sweep [name=value ...] [name=start:stop:count ...] [name=v1,v2,... ...] [out=FILE]\n"
          "Runs one mission at every combination of the swept settings and writes a row of results for each, as CSV.\n"
          "name=250 fixes a setting; name=160:300:8 sweeps 8 values from 160 to 300; name=10,12,14 sweeps a list.\n"
          "At most " << abep::kMaxSweepPoints << " points. Each point also runs the time to decay with the thruster off, which can be long above\n"
          "300 km: decay_max_years=0.5 stops that run at half a year (the row then says decayed 0).\n"
          "The defaults are illustrative, not data about any real satellite or thruster.\n\n"
          "settings (name = default, meaning):\n";
    const abep::RunSettings defaults;
    for (const abep::SettingInfo& s : abep::setting_table()) {
        os << "  " << s.key << " = " << abep::format_number(defaults.*(s.field), 6) << "   " << s.meaning << '\n';
    }
    os << "\ncolumns after the swept settings:";
    for (const std::string& key : abep::sweep_output_keys()) os << ' ' << key;
    os << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    try {
        abep::RunSettings settings;
        std::vector<abep::SweepAxis> axes;
        std::set<std::string> seen;
        std::string out_file;
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
                out_file = value;
                continue;
            }
            if (!seen.insert(key).second) throw std::invalid_argument("setting '" + key + "' is given twice");
            if (value.find(':') != std::string::npos || value.find(',') != std::string::npos) {
                axes.push_back(abep::parse_sweep_axis(key, value));
            } else {
                double number = 0.0;
                if (!abep::detail::parse_plain_number(value, number)) {
                    throw std::invalid_argument("'" + value + "' is not a number (setting '" + key + "')");
                }
                abep::set_setting(settings, key, number);
            }
        }
        if (axes.empty()) {
            throw std::invalid_argument("nothing is swept: give a setting a range (name=start:stop:count) or a list (name=v1,v2)");
        }

        const abep::DensityTable atmosphere = abep::ussa1976::density_table();
        const std::size_t points = abep::sweep_size(axes);
        if (out_file.empty()) {
            abep::write_sweep_csv(std::cout, settings, axes, atmosphere);
        } else {
            std::ofstream file(out_file, std::ios::binary);
            if (!file) throw std::runtime_error("could not open " + out_file + " for writing");
            abep::write_sweep_csv(file, settings, axes, atmosphere);
            file.close();
            if (!file) throw std::runtime_error("could not write " + out_file);
        }
        std::fprintf(stderr, "abep_sweep: %zu points written to %s\n", points, out_file.empty() ? "the standard output" : out_file.c_str());
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "abep_sweep: %s\n(abep_sweep --help lists the settings)\n", e.what());
        return 2;
    }
}
