#pragma once

// Unit conversions at the edge of the model. Everything inside abep is SI.
// The literature quotes thrust density in millinewtons per kilowatt, and an
// off-by-a-factor-of-a-million there would silently wreck every result, so the
// conversion lives in one place and is tested.

namespace abep::units {

constexpr double kPi = 3.14159265358979323846;

// 1 mN/kW = 1e-3 N / 1e3 W = 1e-6 N/W.
constexpr double mn_per_kw_to_n_per_w(double mn_per_kw) noexcept { return mn_per_kw * 1e-6; }
constexpr double n_per_w_to_mn_per_kw(double n_per_w) noexcept { return n_per_w * 1e6; }

constexpr double km_to_m(double km) noexcept { return km * 1e3; }
constexpr double m_to_km(double m) noexcept { return m * 1e-3; }

constexpr double deg_to_rad(double deg) noexcept { return deg * kPi / 180.0; }
constexpr double rad_to_deg(double rad) noexcept { return rad * 180.0 / kPi; }

}  // namespace abep::units
