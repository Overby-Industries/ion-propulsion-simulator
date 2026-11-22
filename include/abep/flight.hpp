#pragma once

// The flight state machine. Which of five modes the satellite is in is decided once per control step from three kinds of
// flag (power, density, thermal) and an external fault flag. This header is only the logic: no physics and no clock of
// its own. mission.hpp runs it against the orbit, power and thermal models, and docs/FLIGHT_MODES.md has the transition
// table and says what every threshold means. The thresholds are operating policy, chosen by whoever runs the simulator,
// not physical constants, and none of the defaults is sourced.
//
// Rules, in priority order, applied once per call to update() (at most one transition per call):
//
//   1. A fault condition (the fault flag, or a temperature at or above the fault limit) puts any mode in FAULT.
//   2. FAULT hands over to SAFE after fault_hold_s with no fault condition, and to nothing else.
//   3. Below the state of charge soc_safe_enter, any mode but SAFE goes to SAFE. SAFE goes to CHARGE at soc_safe_exit.
//   4. Below soc_thrust_stop, COAST and THRUST go to CHARGE. CHARGE goes on at soc_thrust_start, to THRUST if the
//      thruster may start and to COAST if not.
//   5. COAST goes to THRUST as soon as the thruster may start, which includes a battery at soc_thrust_start or more:
//      the restart level applies after any stop, so a satellite that coasted through an eclipse recharges before it
//      thrusts again. THRUST goes to COAST as soon as it must stop.

#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>

namespace abep {

enum class Mode {
    Safe,    // survival: thruster off and the bus sheds load. Entered on a deep discharge and after a fault
    Charge,  // thruster off while the battery recharges to the level where thrusting may start
    Coast,   // thruster off although the battery is fine: waiting for sunlight, air or a cooler thruster
    Thrust,  // thruster on
    Fault    // a fault is active, or was within the hold time: thruster off and the bus sheds load
};

inline constexpr std::size_t kModeCount = 5;

constexpr const char* mode_name(Mode m) noexcept {
    switch (m) {
        case Mode::Safe: return "SAFE";
        case Mode::Charge: return "CHARGE";
        case Mode::Coast: return "COAST";
        case Mode::Thrust: return "THRUST";
        case Mode::Fault: return "FAULT";
    }
    return "?";
}

// What the state machine looks at each step.
struct Flags {
    double battery_fraction = 1.0;   // power: stored energy over capacity, 0 to 1
    bool sunlit = true;              // power: the array is producing (false in eclipse, and during an array outage)
    double density_kg_m3 = 0.0;      // density: the air the intake has to work with
    double temperature_k = 0.0;      // thermal: the thruster's temperature
    bool thruster_available = true;  // false if the thruster is not fitted or has failed: THRUST is never entered
    bool fault = false;              // an external fault condition is asserted (a brownout, a scheduled fault, ...)
};

struct ControllerConfig {
    // Power. State of charge as a fraction of capacity. Needs soc_safe_enter < soc_safe_exit and
    // soc_safe_enter < soc_thrust_stop < soc_thrust_start <= 1. The gap between stop and start is the hysteresis that
    // keeps the machine from switching every step.
    double soc_safe_enter = 0.10;
    double soc_safe_exit = 0.30;
    double soc_thrust_stop = 0.20;
    double soc_thrust_start = 0.50;
    bool thrust_in_eclipse = false;  // false: thrust only while the array is producing

    // Density, kg/m^3: the least air the thruster is run in. Starts at or above `start`, stops below `stop`. Zero for
    // both means density is never a reason to stop. Needs stop <= start.
    double min_density_start_kg_m3 = 0.0;
    double min_density_stop_kg_m3 = 0.0;

    // Thermal, K: thrusting stops at `limit`, may start again at or below `resume`, and at `fault` or above is a
    // fault. Needs resume <= limit <= fault. Infinity (the default) means the thermal flag never trips.
    double temperature_resume_k = std::numeric_limits<double>::infinity();
    double temperature_limit_k = std::numeric_limits<double>::infinity();
    double temperature_fault_k = std::numeric_limits<double>::infinity();

    // Seconds with no fault condition before FAULT hands over to SAFE.
    double fault_hold_s = 600.0;
};

// Throws std::invalid_argument for thresholds that are out of order or not numbers.
inline void validate(const ControllerConfig& c) {
    if (!(0.0 <= c.soc_safe_enter && c.soc_safe_enter < c.soc_safe_exit && c.soc_safe_exit <= 1.0)) {
        throw std::invalid_argument("state of charge thresholds need 0 <= safe_enter < safe_exit <= 1");
    }
    if (!(c.soc_safe_enter < c.soc_thrust_stop && c.soc_thrust_stop < c.soc_thrust_start && c.soc_thrust_start <= 1.0)) {
        throw std::invalid_argument("state of charge thresholds need safe_enter < thrust_stop < thrust_start <= 1");
    }
    if (!(std::isfinite(c.min_density_start_kg_m3) && std::isfinite(c.min_density_stop_kg_m3) &&
          0.0 <= c.min_density_stop_kg_m3 && c.min_density_stop_kg_m3 <= c.min_density_start_kg_m3)) {
        throw std::invalid_argument("density thresholds need 0 <= stop <= start, and finite");
    }
    if (!(0.0 < c.temperature_resume_k && c.temperature_resume_k <= c.temperature_limit_k &&
          c.temperature_limit_k <= c.temperature_fault_k)) {
        throw std::invalid_argument("temperature thresholds need 0 < resume <= limit <= fault");
    }
    if (!(std::isfinite(c.fault_hold_s) && c.fault_hold_s >= 0.0)) {
        throw std::invalid_argument("the fault hold time must be zero or more, and finite");
    }
}

// Throws std::invalid_argument for flags that are not numbers or are out of range.
inline void validate(const Flags& f) {
    if (!(f.battery_fraction >= 0.0 && f.battery_fraction <= 1.0)) {
        throw std::invalid_argument("battery fraction must be between 0 and 1");
    }
    if (!(std::isfinite(f.density_kg_m3) && f.density_kg_m3 >= 0.0)) {
        throw std::invalid_argument("density must be zero or more, and finite");
    }
    if (!(std::isfinite(f.temperature_k) && f.temperature_k >= 0.0)) {
        throw std::invalid_argument("temperature must be zero or more, and finite");
    }
}

class FlightComputer {
public:
    // A satellite starts in SAFE unless told otherwise, and works its way up. Throws std::invalid_argument for a bad
    // configuration.
    explicit FlightComputer(const ControllerConfig& config = ControllerConfig{}, Mode initial = Mode::Safe)
        : config_(config), mode_(initial) {
        validate(config_);
    }

    Mode mode() const noexcept { return mode_; }
    const ControllerConfig& config() const noexcept { return config_; }
    // Seconds without a fault condition so far, while in FAULT; zero in every other mode.
    double fault_clear_s() const noexcept { return fault_clear_s_; }

    // One control step of length dt_s. Returns the mode for that step. Throws std::invalid_argument for invalid flags
    // or step.
    Mode update(const Flags& flags, double dt_s) {
        validate(flags);
        if (!(std::isfinite(dt_s) && dt_s >= 0.0)) throw std::invalid_argument("time step must be zero or more");

        const bool fault_now = flags.fault || flags.temperature_k >= config_.temperature_fault_k;
        if (fault_now) {
            fault_clear_s_ = 0.0;
            mode_ = Mode::Fault;
            return mode_;
        }
        if (mode_ == Mode::Fault) {
            fault_clear_s_ += dt_s;
            if (fault_clear_s_ >= config_.fault_hold_s) {
                fault_clear_s_ = 0.0;
                mode_ = Mode::Safe;
            }
            return mode_;
        }

        const double soc = flags.battery_fraction;
        switch (mode_) {
            case Mode::Safe:
                if (soc >= config_.soc_safe_exit) mode_ = Mode::Charge;
                break;
            case Mode::Charge:
                if (soc < config_.soc_safe_enter) {
                    mode_ = Mode::Safe;
                } else if (soc >= config_.soc_thrust_start) {
                    mode_ = may_start_thrust(flags) ? Mode::Thrust : Mode::Coast;
                }
                break;
            case Mode::Coast:
                if (soc < config_.soc_safe_enter) {
                    mode_ = Mode::Safe;
                } else if (soc < config_.soc_thrust_stop) {
                    mode_ = Mode::Charge;
                } else if (may_start_thrust(flags)) {
                    mode_ = Mode::Thrust;
                }
                break;
            case Mode::Thrust:
                if (soc < config_.soc_safe_enter) {
                    mode_ = Mode::Safe;
                } else if (soc < config_.soc_thrust_stop) {
                    mode_ = Mode::Charge;
                } else if (must_stop_thrust(flags)) {
                    mode_ = Mode::Coast;
                }
                break;
            case Mode::Fault:
                break;  // handled above
        }
        return mode_;
    }

private:
    // The conditions to start thrusting, after any stop: a thruster, power, a battery charged to the restart level,
    // enough air, and a thruster cool enough.
    bool may_start_thrust(const Flags& f) const noexcept {
        return f.thruster_available && (f.sunlit || config_.thrust_in_eclipse) &&
               f.battery_fraction >= config_.soc_thrust_start && f.density_kg_m3 >= config_.min_density_start_kg_m3 &&
               f.temperature_k <= config_.temperature_resume_k;
    }

    // The conditions to stop: any of them. The density and temperature thresholds differ from the start ones, so a
    // flag that wanders around its threshold does not make the mode flip every step.
    bool must_stop_thrust(const Flags& f) const noexcept {
        return !f.thruster_available || (!f.sunlit && !config_.thrust_in_eclipse) ||
               f.density_kg_m3 < config_.min_density_stop_kg_m3 || f.temperature_k >= config_.temperature_limit_k;
    }

    ControllerConfig config_;
    Mode mode_;
    double fault_clear_s_ = 0.0;
};

}  // namespace abep
