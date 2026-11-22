// The abep Python module: the same run as `abep_run`, from Python, with the results as plain dicts and lists (so that
// pandas.DataFrame(result["samples"]) and matplotlib work without anything else installed). Built with -DABEP_BUILD_PYTHON=ON,
// which needs pybind11. The functions are described in docs/OUTPUT.md.
//
//   import abep
//   r = abep.run(altitude_km=300, mass_kg=150, duration_days=2)
//   r["samples"]["altitude_km"], r["orbits"]["duty"], r["summary"]["hold_power_w"]
//   abep.write_csv("my_run", altitude_km=300)        # the same three CSV files abep_run writes
//   abep.settings()                                  # every setting and its default

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <istream>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

#include "abep/csv.hpp"
#include "abep/full_run.hpp"
#include "abep/run_config.hpp"
#include "abep/standard_atmosphere_1976.hpp"
#include "abep/version.hpp"

namespace py = pybind11;

namespace {

const abep::DensityTable& atmosphere() {
    static const abep::DensityTable table = abep::ussa1976::density_table();
    return table;
}

// Keyword arguments to settings. A bool counts as 0 or 1; anything that is not a number is a TypeError, and a name that is not a
// setting, or a value that makes no sense, is a ValueError (std::invalid_argument is translated to it).
abep::RunSettings settings_from(const py::kwargs& kwargs) {
    abep::RunSettings settings;
    for (const auto& item : kwargs) {
        const std::string key = py::cast<std::string>(item.first);
        const py::handle value = item.second;
        if (!(py::isinstance<py::int_>(value) || py::isinstance<py::float_>(value))) {
            throw py::type_error("setting '" + key + "' needs a number");
        }
        abep::set_setting(settings, key, py::cast<double>(value));
    }
    return settings;
}

// A summary value is a number unless it is text (an end reason, a hold status).
py::object summary_value(const std::string& text) {
    std::istringstream in(text);
    in.imbue(std::locale::classic());
    double x = 0.0;
    in >> x;
    if (!in.fail() && in.peek() == std::char_traits<char>::eof()) return py::float_(x);
    return py::str(text);
}

py::dict samples_columns(const std::vector<abep::Sample>& samples) {
    std::vector<double> t, altitude_km, density, speed, drag, thrust, net_drag, thruster_w, generation, load, battery, temperature;
    std::vector<std::string> mode;
    std::vector<int> sunlit;
    for (const abep::Sample& s : samples) {
        t.push_back(s.t_s);
        mode.emplace_back(abep::mode_name(s.mode));
        altitude_km.push_back(abep::units::m_to_km(s.altitude_m));
        density.push_back(s.density_kg_m3);
        speed.push_back(s.relative_speed_m_s);
        drag.push_back(s.drag_n);
        thrust.push_back(s.thrust_n);
        net_drag.push_back(s.net_drag_n);
        thruster_w.push_back(s.thruster_power_w);
        generation.push_back(s.generation_w);
        load.push_back(s.load_w);
        battery.push_back(s.battery_wh);
        temperature.push_back(s.temperature_k);
        sunlit.push_back(s.sunlit ? 1 : 0);
    }
    py::dict d;
    d["t_s"] = t;
    d["mode"] = mode;
    d["altitude_km"] = altitude_km;
    d["density_kg_m3"] = density;
    d["relative_speed_m_s"] = speed;
    d["drag_n"] = drag;
    d["thrust_n"] = thrust;
    d["net_drag_n"] = net_drag;
    d["thruster_power_w"] = thruster_w;
    d["generation_w"] = generation;
    d["load_w"] = load;
    d["battery_wh"] = battery;
    d["temperature_k"] = temperature;
    d["sunlit"] = sunlit;
    return d;
}

py::dict orbits_columns(const std::vector<abep::OrbitBalance>& orbits) {
    std::vector<int> index;
    std::vector<double> duration, thrust, duty, eclipse, generated, load, thruster, battery_change, spilled;
    for (std::size_t i = 0; i < orbits.size(); ++i) {
        const abep::OrbitBalance& b = orbits[i];
        index.push_back(static_cast<int>(i));
        duration.push_back(b.duration_s);
        thrust.push_back(b.thrust_s);
        duty.push_back(b.duty());
        eclipse.push_back(b.eclipse_s);
        generated.push_back(b.generated_wh);
        load.push_back(b.load_wh);
        thruster.push_back(b.thruster_wh);
        battery_change.push_back(b.battery_change_wh);
        spilled.push_back(b.spilled_wh);
    }
    py::dict d;
    d["orbit"] = index;
    d["duration_s"] = duration;
    d["thrust_s"] = thrust;
    d["duty"] = duty;
    d["eclipse_s"] = eclipse;
    d["generated_wh"] = generated;
    d["load_wh"] = load;
    d["thruster_wh"] = thruster;
    d["battery_change_wh"] = battery_change;
    d["spilled_wh"] = spilled;
    return d;
}

// The simulation can take a while; the interpreter is free to do other things meanwhile.
abep::FullRun simulate_without_gil(const abep::RunSettings& settings) {
    py::gil_scoped_release release;
    return abep::simulate(settings, atmosphere());
}

}  // namespace

PYBIND11_MODULE(abep, m) {
    m.doc() =
        "ABEP: Ion Thrusters Simulator. An open, deterministic sensitivity tool for how much power an air-breathing electric "
        "thruster needs to hold a satellite's altitude. run(**settings) returns {'samples', 'orbits', 'summary'}; "
        "write_csv(directory, **settings) writes the files abep_run writes; settings() lists every setting and its default.";
    m.attr("__version__") = abep::version();

    m.def(
        "settings",
        [] {
            const abep::RunSettings defaults;
            py::dict d;
            for (const abep::SettingInfo& s : abep::setting_table()) d[s.key] = defaults.*(s.field);
            return d;
        },
        "Every setting and its default value.");

    m.def(
        "run",
        [](const py::kwargs& kwargs) {
            const abep::RunSettings settings = settings_from(kwargs);
            const abep::FullRun result = simulate_without_gil(settings);
            py::dict summary;
            for (const abep::KeyValue& kv : abep::summary_rows(result)) summary[kv.key.c_str()] = summary_value(kv.value);
            py::dict out;
            out["samples"] = samples_columns(result.run.samples);
            out["orbits"] = orbits_columns(result.run.orbits);
            out["summary"] = summary;
            return out;
        },
        "Run a mission described by keyword settings (see settings()). Returns a dict of three dicts: 'samples' and 'orbits' map "
        "column names to lists, one entry per kept step or per orbit; 'summary' maps the keys of summary.csv to numbers or text. "
        "Raises ValueError for a setting that does not exist or a value that makes no sense, and TypeError for a value that is "
        "not a number.");

    m.def(
        "write_csv",
        [](const std::string& directory, const py::kwargs& kwargs) {
            const abep::RunSettings settings = settings_from(kwargs);
            const abep::FullRun result = simulate_without_gil(settings);
            abep::write_run_files(result, directory);
            return directory;
        },
        py::arg("directory"),
        "Run a mission and write samples.csv, orbits.csv and summary.csv into the directory, as the abep_run program does. "
        "Returns the directory.");
}
