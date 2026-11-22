#pragma once

// Parameter sweeps (docs/SWEEPS.md): one mission, run over a grid of settings, with one row of results for each point. A sweep is a
// base set of settings and a list of axes; an axis is one setting and the values it takes. The grid is the Cartesian product of the
// axes, in a fixed order: the last axis changes fastest. Every point is an ordinary run, the one abep_run does (simulate() in
// full_run.hpp), so a row holds the numbers the single run gives, character for character, and a sweep is as deterministic as a
// run is.

#include <cmath>
#include <cstddef>
#include <locale>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "abep/atmosphere.hpp"
#include "abep/csv.hpp"
#include "abep/full_run.hpp"
#include "abep/run_config.hpp"

namespace abep {

struct SweepAxis {
    std::string name;            // a setting of RunSettings
    std::vector<double> values;  // at least one
};

// The most points one sweep may have, and the most values one axis may have. A typo (a thousand values on each of three axes)
// should not start a run of days.
inline constexpr std::size_t kMaxSweepPoints = 100000;

namespace detail {

// A number in the "C" locale, with nothing after it.
inline bool parse_plain_number(const std::string& text, double& out) {
    if (text.empty()) return false;
    std::istringstream in(text);
    in.imbue(std::locale::classic());
    in >> out;
    return !in.fail() && in.peek() == std::char_traits<char>::eof();
}

inline std::vector<std::string> split_on(const std::string& text, char separator) {
    std::vector<std::string> parts(1);
    for (const char c : text) {
        if (c == separator) {
            parts.emplace_back();
        } else {
            parts.back().push_back(c);
        }
    }
    return parts;
}

// The value of one setting, by name (0 for a name that is not a setting: sweep_size() and parse_sweep_axis() have seen to that).
inline double setting_value(const RunSettings& settings, const std::string& name) {
    for (const SettingInfo& info : setting_table()) {
        if (name == info.key) return settings.*(info.field);
    }
    return 0.0;
}

}  // namespace detail

// The values of an axis, from text: "start:stop:count" (count values, equally spaced, both ends included; a count of 1 gives just
// start) or "v1,v2,..." (a list; one number alone is a list of one). Throws std::invalid_argument, naming the setting, for
// anything else.
inline std::vector<double> parse_sweep_values(const std::string& name, const std::string& text) {
    const auto fail = [&name](const std::string& why) { throw std::invalid_argument("sweep of '" + name + "': " + why); };
    std::vector<double> values;
    if (text.find(':') != std::string::npos) {
        const std::vector<std::string> parts = detail::split_on(text, ':');
        if (parts.size() != 3) fail("'" + text + "' is not start:stop:count");
        double start = 0.0;
        double stop = 0.0;
        double count_value = 0.0;
        if (!detail::parse_plain_number(parts[0], start) || !detail::parse_plain_number(parts[1], stop) || !std::isfinite(start) ||
            !std::isfinite(stop)) {
            fail("'" + text + "' needs a number for the start and for the stop");
        }
        if (!detail::parse_plain_number(parts[2], count_value) || !(count_value >= 1.0) || count_value != std::floor(count_value) ||
            count_value > static_cast<double>(kMaxSweepPoints)) {
            fail("'" + parts[2] + "' is not a count from 1 to " + std::to_string(kMaxSweepPoints));
        }
        const std::size_t count = static_cast<std::size_t>(count_value);
        for (std::size_t i = 0; i < count; ++i) {
            if (count == 1 || i == 0) {
                values.push_back(start);
            } else if (i + 1 == count) {
                values.push_back(stop);  // exactly, whatever the rounding of the steps before it
            } else {
                values.push_back(start + (stop - start) * static_cast<double>(i) / static_cast<double>(count - 1));
            }
        }
    } else {
        for (const std::string& part : detail::split_on(text, ',')) {
            double v = 0.0;
            if (!detail::parse_plain_number(part, v) || !std::isfinite(v)) fail("'" + part + "' is not a number");
            values.push_back(v);
        }
    }
    return values;
}

// One axis: the setting `name` (it has to be one of the settings) and its values, in the text above. Throws std::invalid_argument.
inline SweepAxis parse_sweep_axis(const std::string& name, const std::string& text) {
    RunSettings probe;
    set_setting(probe, name, 0.0);  // throws "unknown setting 'name'"
    return SweepAxis{name, parse_sweep_values(name, text)};
}

// The number of points in the grid. Throws std::invalid_argument if there is no axis, an axis has no values, a setting is swept
// twice, or the grid has more than kMaxSweepPoints points.
inline std::size_t sweep_size(const std::vector<SweepAxis>& axes) {
    if (axes.empty()) throw std::invalid_argument("a sweep needs at least one axis");
    std::size_t points = 1;
    for (std::size_t i = 0; i < axes.size(); ++i) {
        if (axes[i].values.empty()) throw std::invalid_argument("sweep of '" + axes[i].name + "' has no values");
        for (std::size_t j = 0; j < i; ++j) {
            if (axes[j].name == axes[i].name) throw std::invalid_argument("setting '" + axes[i].name + "' is swept twice");
        }
        if (points > kMaxSweepPoints / axes[i].values.size()) {
            throw std::invalid_argument("the sweep has more than " + std::to_string(kMaxSweepPoints) + " points");
        }
        points *= axes[i].values.size();
    }
    return points;
}

// The settings of one point of the grid, counted from 0 with the last axis changing fastest: the base settings with the axes'
// values put in.
inline RunSettings sweep_point(const RunSettings& base, const std::vector<SweepAxis>& axes, std::size_t index) {
    RunSettings settings = base;
    for (std::size_t k = axes.size(); k-- > 0;) {
        const SweepAxis& axis = axes[k];
        set_setting(settings, axis.name, axis.values[index % axis.values.size()]);
        index /= axis.values.size();
    }
    return settings;
}

// The columns that follow the swept settings in a sweep's CSV: rows of summary.csv (docs/OUTPUT.md), written as that file writes
// them. A column is empty where the run has no such row (no hold power if the satellite leaves the density table within an orbit).
inline const std::vector<std::string>& sweep_output_keys() {
    static const std::vector<std::string> keys = {"end_reason",      "thrust_duty",          "final_altitude_km",
                                                  "decayed",         "decay_time_days",      "hold_status",
                                                  "hold_power_w",    "hold_mean_drag_n",     "hold_mean_intake_limit_n",
                                                  "hold_array_power_w"};
    return keys;
}

namespace detail {

// The sweep, with `simulate_point(settings)` giving the FullRun of each point.
template <typename SimulatePoint>
void write_sweep_csv_with(std::ostream& os, const RunSettings& base, const std::vector<SweepAxis>& axes,
                          SimulatePoint&& simulate_point) {
    const std::size_t points = sweep_size(axes);
    const auto describe = [&](const RunSettings& s) {
        std::string text;
        for (const SweepAxis& axis : axes) {
            text += (text.empty() ? "" : ", ") + axis.name + "=" + format_number(detail::setting_value(s, axis.name), 9);
        }
        return text;
    };
    const auto where = [&](std::size_t index, const RunSettings& s) {
        return "sweep point " + std::to_string(index + 1) + " of " + std::to_string(points) + " (" + describe(s) + "): ";
    };
    for (std::size_t i = 0; i < points; ++i) {
        const RunSettings s = sweep_point(base, axes, i);
        try {
            (void)make_mission(s);
        } catch (const std::invalid_argument& e) {
            throw std::invalid_argument(where(i, s) + e.what());
        }
    }

    std::string header;
    for (const SweepAxis& axis : axes) header += axis.name + ",";
    const std::vector<std::string>& keys = sweep_output_keys();
    for (std::size_t k = 0; k < keys.size(); ++k) header += keys[k] + (k + 1 < keys.size() ? "," : "");
    os << header << '\n';

    for (std::size_t i = 0; i < points; ++i) {
        const RunSettings s = sweep_point(base, axes, i);
        std::vector<KeyValue> summary;
        try {
            summary = summary_rows(simulate_point(s));
        } catch (const std::invalid_argument& e) {
            throw std::invalid_argument(where(i, s) + e.what());
        }
        std::string row;
        for (const SweepAxis& axis : axes) row += format_number(detail::setting_value(s, axis.name)) + ",";
        for (std::size_t k = 0; k < keys.size(); ++k) {
            for (const KeyValue& kv : summary) {
                if (kv.key == keys[k]) row += kv.value;
            }
            if (k + 1 < keys.size()) row += ",";
        }
        os << row << '\n';
        os.flush();
    }
}

}  // namespace detail

// Runs the sweep and writes it as CSV: a header (the swept settings, then sweep_output_keys()) and one row per point, each flushed
// as it is written, so that a long sweep can be watched. The settings of every point are checked before the first run, so that a
// point that makes no sense (a capture efficiency of 1.5, an atmosphere of 9) is found at once; the error names the point and its
// values. Each point flies the atmosphere that its `atmosphere` setting names, so the atmosphere can be swept too.
// Throws std::invalid_argument.
inline void write_sweep_csv(std::ostream& os, const RunSettings& base, const std::vector<SweepAxis>& axes) {
    detail::write_sweep_csv_with(os, base, axes, [](const RunSettings& s) { return simulate(s); });
}

// The same with one density table for every point, whatever the `atmosphere` setting says; so the setting cannot be swept, and
// asking for it throws std::invalid_argument.
inline void write_sweep_csv(std::ostream& os, const RunSettings& base, const std::vector<SweepAxis>& axes,
                            const DensityTable& atmosphere) {
    for (const SweepAxis& axis : axes) {
        if (axis.name == "atmosphere") {
            throw std::invalid_argument("the atmosphere is fixed by the density table given, so the setting 'atmosphere' cannot be swept");
        }
    }
    detail::write_sweep_csv_with(os, base, axes, [&](const RunSettings& s) { return simulate(s, atmosphere); });
}

}  // namespace abep
