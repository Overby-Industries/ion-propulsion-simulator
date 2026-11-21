#pragma once

// Electrical power: a solar array, an ideal battery, and the orbit-averaged power budget. Sunlight and shadow come
// from eclipse.hpp. The battery is ideal: no charge or discharge losses, no rate limit, no depth-of-discharge or
// ageing model. Array degradation is not modelled either: give the end-of-life efficiency if that is what you want.

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "abep/constants.hpp"

namespace abep {

// Electrical power from an array of the given area and efficiency, W:  S * A * eta * cos(incidence).
// `cos_incidence` is the cosine of the angle between the Sun direction and the array normal; zero or negative means
// the Sun is behind the array. The defaults are the IAU nominal irradiance and an array that faces the Sun; there is
// no attitude model, so the array is assumed to point at the Sun whenever it is lit. Throws std::invalid_argument
// for a negative area or irradiance, or an efficiency outside 0 to 1.
inline double solar_array_power(double area_m2, double efficiency, double cos_incidence = 1.0,
                                double irradiance_w_m2 = constants::kSolarConstant) {
    if (!(std::isfinite(area_m2) && area_m2 >= 0.0)) throw std::invalid_argument("array area must be zero or more");
    if (!(efficiency >= 0.0 && efficiency <= 1.0)) throw std::invalid_argument("array efficiency must be between 0 and 1");
    if (!(std::isfinite(irradiance_w_m2) && irradiance_w_m2 >= 0.0)) throw std::invalid_argument("irradiance must be zero or more");
    if (!(cos_incidence >= -1.0 && cos_incidence <= 1.0)) throw std::invalid_argument("cos(incidence) must be between -1 and 1");
    if (cos_incidence <= 0.0) return 0.0;
    return irradiance_w_m2 * area_m2 * efficiency * cos_incidence;
}

class Battery {
public:
    // Throws std::invalid_argument unless 0 < capacity and 0 <= initial energy <= capacity.
    Battery(double capacity_wh, double initial_energy_wh) : capacity_wh_(capacity_wh), energy_wh_(initial_energy_wh) {
        if (!(std::isfinite(capacity_wh) && capacity_wh > 0.0)) throw std::invalid_argument("battery capacity must be positive");
        if (!(initial_energy_wh >= 0.0 && initial_energy_wh <= capacity_wh)) {
            throw std::invalid_argument("initial energy must be between 0 and the capacity");
        }
    }

    double capacity_wh() const noexcept { return capacity_wh_; }
    double energy_wh() const noexcept { return energy_wh_; }

    // Apply `power_w` for `dt_s` seconds: positive charges, negative discharges. The store saturates at empty and at
    // full. Returns the change in stored energy actually made, Wh. That is power * dt / 3600 unless a limit was reached,
    // and the caller can read what was refused (excess charge, or a shortfall) as the difference.
    double apply(double power_w, double dt_s) {
        if (!std::isfinite(power_w)) throw std::invalid_argument("power must be finite");
        if (!(std::isfinite(dt_s) && dt_s >= 0.0)) throw std::invalid_argument("time step must be zero or more");
        const double wanted = power_w * dt_s / 3600.0;
        const double after = std::min(capacity_wh_, std::max(0.0, energy_wh_ + wanted));
        const double change = after - energy_wh_;
        energy_wh_ = after;
        return change;
    }

private:
    double capacity_wh_;
    double energy_wh_;
};

// The share of the time the thruster can run, averaged over orbits, once the bus is served:
//     clamp( (P_array * f_sun - P_load) / P_thruster, 0, 1 ).
// It assumes an ideal battery big enough to carry the load through the eclipse, and a thruster that runs at its full
// power `thruster_power_w` or not at all. `sunlit_fraction` is 1 minus the eclipse fraction.
inline double sustainable_thrust_duty(double array_power_w, double sunlit_fraction, double bus_load_w,
                                      double thruster_power_w) {
    if (!(std::isfinite(array_power_w) && array_power_w >= 0.0)) throw std::invalid_argument("array power must be zero or more");
    if (!(sunlit_fraction >= 0.0 && sunlit_fraction <= 1.0)) throw std::invalid_argument("sunlit fraction must be between 0 and 1");
    if (!(std::isfinite(bus_load_w) && bus_load_w >= 0.0)) throw std::invalid_argument("bus load must be zero or more");
    if (!(std::isfinite(thruster_power_w) && thruster_power_w > 0.0)) throw std::invalid_argument("thruster power must be positive");
    return std::min(1.0, std::max(0.0, (array_power_w * sunlit_fraction - bus_load_w) / thruster_power_w));
}

}  // namespace abep
