#pragma once

// Which atmosphere a run flies through, as the `atmosphere` setting names it: 0 for the 1976 US Standard Atmosphere (the
// default), or 1 to 4 for the four levels of solar and geomagnetic activity of ECSS-E-ST-10-04C Annex G. The tables themselves are
// in standard_atmosphere_1976.hpp and solar_activity_atmosphere.hpp, and full_run.hpp turns a choice into a table. See
// docs/ATMOSPHERE.md.

#include <stdexcept>

namespace abep {

enum class AtmosphereChoice {
    Standard1976 = 0,
    EcssLow = 1,
    EcssModerate = 2,
    EcssHigh = 3,
    EcssHighShortTerm = 4,
};

// The choice a setting value stands for. Throws std::invalid_argument, naming the setting, for anything but 0, 1, 2, 3 or 4
// (including NaN).
inline AtmosphereChoice atmosphere_choice(double setting) {
    if (setting == 0.0) return AtmosphereChoice::Standard1976;
    if (setting == 1.0) return AtmosphereChoice::EcssLow;
    if (setting == 2.0) return AtmosphereChoice::EcssModerate;
    if (setting == 3.0) return AtmosphereChoice::EcssHigh;
    if (setting == 4.0) return AtmosphereChoice::EcssHighShortTerm;
    throw std::invalid_argument(
        "setting 'atmosphere' must be 0 (the 1976 standard), 1 (ECSS low), 2 (moderate), 3 (high, long term) or 4 (high, short term)");
}

}  // namespace abep
