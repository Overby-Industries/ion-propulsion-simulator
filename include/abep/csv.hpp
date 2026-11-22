#pragma once

// Results as CSV: plain text with one header row, readable by a spreadsheet, pandas, or a few lines of any language. Columns are
// named with their unit. Numbers are written with 17 significant digits by default, so that they read back as the same double,
// and with a decimal point and no digit grouping whatever the locale of the program or of the stream; the stream's own settings
// are put back afterwards. A value that is not finite is written as the stream writes it ("inf", "nan"). The columns, and what
// each one means, are in docs/OUTPUT.md.

#include <cstddef>
#include <ios>
#include <locale>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "abep/flight.hpp"
#include "abep/mission.hpp"
#include "abep/orbit.hpp"
#include "abep/summary.hpp"
#include "abep/units.hpp"

namespace abep {

namespace detail {

inline void check_digits(int digits) {
    if (digits < 1 || digits > 17) throw std::invalid_argument("significant digits must be from 1 to 17");
}

// Sets the formatting a CSV needs on a stream, and puts the stream back as it was.
class CsvFormat {
public:
    CsvFormat(std::ostream& os, int digits)
        : os_(os), flags_(os.flags()), precision_(os.precision()), locale_(os.imbue(std::locale::classic())) {
        os_.flags(std::ios_base::dec);  // no field width rules, no sign or point forcing, the general float format
        os_.precision(digits);
    }
    ~CsvFormat() {
        os_.flags(flags_);
        os_.precision(precision_);
        os_.imbue(locale_);
    }
    CsvFormat(const CsvFormat&) = delete;
    CsvFormat& operator=(const CsvFormat&) = delete;

private:
    std::ostream& os_;
    std::ios_base::fmtflags flags_;
    std::streamsize precision_;
    std::locale locale_;
};

}  // namespace detail

// A number as the CSV writers write it.
inline std::string format_number(double x, int digits = 17) {
    detail::check_digits(digits);
    std::ostringstream s;
    const detail::CsvFormat format(s, digits);
    s << x;
    return s.str();
}

// One row per sample. Altitude is in km; every other column is in the unit its name gives. `sunlit` is 1 or 0.
inline void write_samples_csv(std::ostream& os, const std::vector<Sample>& samples, int digits = 17) {
    detail::check_digits(digits);
    const detail::CsvFormat format(os, digits);
    os << "t_s,mode,altitude_km,density_kg_m3,relative_speed_m_s,drag_n,thrust_n,net_drag_n,thruster_power_w,generation_w,"
          "load_w,battery_wh,temperature_k,sunlit\n";
    for (const Sample& s : samples) {
        os << s.t_s << ',' << mode_name(s.mode) << ',' << units::m_to_km(s.altitude_m) << ',' << s.density_kg_m3 << ','
           << s.relative_speed_m_s << ',' << s.drag_n << ',' << s.thrust_n << ',' << s.net_drag_n << ','
           << s.thruster_power_w << ',' << s.generation_w << ',' << s.load_w << ',' << s.battery_wh << ','
           << s.temperature_k << ',' << (s.sunlit ? 1 : 0) << '\n';
    }
}

// One row per orbit: the books of MissionResult::orbits. `orbit` counts from 0; `duty` is thrust_s / duration_s.
inline void write_orbits_csv(std::ostream& os, const std::vector<OrbitBalance>& orbits, int digits = 17) {
    detail::check_digits(digits);
    const detail::CsvFormat format(os, digits);
    os << "orbit,duration_s,thrust_s,duty,eclipse_s,generated_wh,load_wh,thruster_wh,battery_change_wh,spilled_wh\n";
    for (std::size_t i = 0; i < orbits.size(); ++i) {
        const OrbitBalance& b = orbits[i];
        os << i << ',' << b.duration_s << ',' << b.thrust_s << ',' << b.duty() << ',' << b.eclipse_s << ',' << b.generated_wh
           << ',' << b.load_wh << ',' << b.thruster_wh << ',' << b.battery_change_wh << ',' << b.spilled_wh << '\n';
    }
}

// A summary is a list of named values, written as `key,value` rows. Keys and values never contain a comma or a line break.
struct KeyValue {
    std::string key;
    std::string value;
};

inline void write_key_values_csv(std::ostream& os, const std::vector<KeyValue>& rows) {
    for (const KeyValue& r : rows) {
        if (r.key.find_first_of(",\n\r\"") != std::string::npos || r.value.find_first_of(",\n\r\"") != std::string::npos) {
            throw std::invalid_argument("a key or a value contains a comma, a quote or a line break");
        }
    }
    os << "key,value\n";
    for (const KeyValue& r : rows) os << r.key << ',' << r.value << '\n';
}

// The totals of a run: its length and end, the time in each mode, the thrust duty, and where the satellite and its battery ended.
inline std::vector<KeyValue> mission_summary(const MissionResult& r, int digits = 17) {
    const auto num = [digits](double x) { return format_number(x, digits); };
    std::vector<KeyValue> rows;
    rows.push_back({"elapsed_s", num(r.elapsed_s)});
    rows.push_back({"end_reason", end_reason_name(r.end_reason)});
    rows.push_back({"orbits", std::to_string(r.orbits.size())});
    rows.push_back({"thrust_duty", num(r.thrust_duty())});
    for (std::size_t m = 0; m < kModeCount; ++m) {
        rows.push_back({std::string("seconds_in_") + mode_name(static_cast<Mode>(m)), num(r.seconds_in_mode[m])});
    }
    rows.push_back({"final_altitude_km", num(units::m_to_km(altitude_above_ellipsoid(r.final_state.r)))});
    rows.push_back({"final_battery_wh", num(r.final_battery_wh)});
    rows.push_back({"final_temperature_k", num(r.final_temperature_k)});
    return rows;
}

inline std::vector<KeyValue> decay_rows(const DecayResult& d, int digits = 17) {
    const auto num = [digits](double x) { return format_number(x, digits); };
    return {{"decayed", d.decayed ? "1" : "0"},
            {"decay_time_s", num(d.time_s)},
            {"decay_time_days", num(d.time_s / 86400.0)},
            {"decay_final_altitude_km", num(units::m_to_km(d.final_altitude_m))}};
}

inline std::vector<KeyValue> hold_rows(const HoldResult& h, int digits = 17) {
    const auto num = [digits](double x) { return format_number(x, digits); };
    return {{"hold_status", hold_status_name(h.status)},
            {"hold_power_w", num(h.power_w)},
            {"hold_mean_drag_n", num(h.mean_drag_n)},
            {"hold_mean_intake_limit_n", num(h.mean_intake_limit_n)},
            {"hold_sunlit_fraction", num(h.sunlit_fraction)},
            {"hold_array_power_w", num(h.array_power_w)}};
}

}  // namespace abep
