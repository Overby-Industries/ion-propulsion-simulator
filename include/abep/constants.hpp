#pragma once

// Earth constants, SI. Sources and what has and has not been checked are in
// docs/CONSTANTS.md; do not change a value without updating that page.

namespace abep::constants {

// WGS 84 defining parameters (NGA.STND.0036, formerly NIMA TR8350.2).
inline constexpr double kEarthEquatorialRadius = 6378137.0;   // m
inline constexpr double kEarthInverseFlattening = 298.257223563;
inline constexpr double kEarthGM = 3.986004418e14;            // m^3/s^2, includes the atmosphere
inline constexpr double kEarthRotationRate = 7.292115e-5;     // rad/s

// Second-degree zonal harmonic of the WGS 84 ellipsoid. Not a defining
// parameter: derived from the four above (1.08262982131330627...e-3, computed
// to 60 digits; this is that value rounded to a double).
// tests/test_constants.cpp recomputes it from the defining parameters and fails
// if this literal drifts.
inline constexpr double kEarthJ2 = 1.0826298213133063e-3;

// Nominal total solar irradiance at 1 au: IAU 2015 Resolution B3 (Prsa et al. 2016, AJ 152, 41). A conversion
// constant fixed by convention, not a measurement. The Earth-Sun distance changes the real value by about 3%
// either way through the year; that is not modelled.
inline constexpr double kSolarConstant = 1361.0;  // W/m^2

}  // namespace abep::constants
