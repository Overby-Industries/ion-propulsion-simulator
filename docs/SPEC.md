# ABEP: Ion Thrusters Simulator -- specification

**Status:** version 0.1.0. This describes what v0.1 does. Written and tested: the orbit propagator (two-body gravity plus J2), drag, the altitude above the ellipsoid, the 1976 US Standard Atmosphere density table, the ABEP thrust model (limited by power or by the intake), the eclipse and solar-power model, and an ideal battery ([ABEP_MODEL.md](ABEP_MODEL.md)), and the flight state machine with a thruster thermal node and a mission run that drives it ([FLIGHT_MODES.md](FLIGHT_MODES.md)). The CSV output, the two summary outputs (time to decay with the thruster off, power to hold the altitude) and a command-line program and a Python module (pybind11) that run the simulation and write or return them are written and tested too ([OUTPUT.md](OUTPUT.md)); the module is tested on Linux only. [VALIDATION.md](VALIDATION.md) says what has been checked.

## The question

For a satellite of given mass, frontal area and drag coefficient in a near-circular low Earth orbit: how much electrical power must an air-breathing electric thruster (ABEP) have to offset atmospheric drag, and what do net drag, altitude and thruster duty cycle look like over time, including eclipses?

This simulator is a **sensitivity tool**. It shows how the answer moves when assumptions change. It does not predict the performance of any real thruster (see [Limits](../README.md#limits)).

## Inputs

SI internally. The unit shown is how the value is usually quoted and accepted.

| Input | Symbol | Unit | Notes |
|---|---|---|---|
| Altitude | h | km | Orbit radius minus the equatorial radius, for a circular orbit (so the altitude at the equator). The altitude used for density is computed along the orbit from the ellipsoid; see Method. Near-circular orbits in v0.1. |
| Inclination | i | deg | Needed for J2 precession and for eclipse geometry. |
| Mass | m | kg | **Not in the original input list**, but drag deceleration is force divided by mass, so altitude over time cannot be computed without it. |
| Frontal area | A | m^2 | Held constant; there is no attitude dynamics. |
| Drag coefficient | Cd | - | The validation case uses a value near 2. |
| Thrust density | tau | mN/kW | Thrust per unit electrical power. The plan assumes 10 to 13.7 mN/kW; that traces to a secondary source, the JAXA concept as reported by Romano et al. (2018), and the exact figure and the primary paper are unread ([VALIDATION.md](VALIDATION.md), finding 12). Stored internally as N/W. |
| Exhaust velocity | c_e | m/s | **Not in the original input list.** The intake can only supply so much air, and what thrust that air makes depends on how fast it leaves. Needed for the intake limit. The model refuses a value that, with the thrust density, would make the thruster more than 100% efficient. No range is sourced yet. |
| Thruster power | P_t | W | Electrical power drawn while thrusting. |
| Array power | P_a | W | Solar array output in sunlight. *(ABEP model)* |
| Battery energy | E_b | Wh | Usable energy for eclipse and thrusting. *(ABEP model)* |
| Bus load | P_l | W | Housekeeping load. *(ABEP model)* |
| Intake area, capture efficiency | A_i, eta_c | m^2, - | Limit thrust by the propellant the intake can collect: `F <= eta_c * rho * v_rel * A_in * c_e` (Romano et al. 2018, Eq. 9; [ABEP_MODEL.md](ABEP_MODEL.md)). The least settled part of the model: real intake efficiency is unproven. *(ABEP model)* |

## Outputs

| Output | Unit | Notes |
|---|---|---|
| Net drag vs time | N | Drag minus thrust; positive decelerates the satellite. |
| Altitude vs time | km | |
| Thruster duty cycle | - | Fraction of time in THRUST, per orbit and over the whole run. |
| Mode timeline | - | SAFE, CHARGE, COAST, THRUST or FAULT. *(state machine)* |
| Summary | | Time to decay with the thruster off; steady-state power to hold altitude; energy balance per orbit. |

Results are written as CSV: the columns, the two summary definitions and the command-line program are in [OUTPUT.md](OUTPUT.md). The Python bindings (pybind11) for plotting return the same results as lists, with no plotting library needed. An optional orbit view in Godot, [`viewer/`](../viewer/README.md), plays the CSV files back as a picture; it computes nothing of its own. A second program, `abep_sweep`, runs the mission over a grid of settings and writes one row of results per point ([SWEEPS.md](SWEEPS.md)); the limits of the model are in [LIMITATIONS.md](LIMITATIONS.md).

## Method (v0.1)

- **Frame and units.** Earth-centred inertial, SI. Gravity is a point mass plus the J2 term, which carries Earth's oblateness; no higher harmonics, no third bodies, no solar radiation pressure.
- **Integration.** Fixed-step RK4. Deterministic: no adaptive steps, no random numbers, no fast-math.
- **Altitude.** For the density lookup, the distance above the WGS 84 ellipsoid along the geocentric radius. Measuring against the equatorial radius instead would be wrong by up to 21 km at the poles (13 km at 51.6 deg latitude), which changes density by tens of percent; this form is within 5 m of true geodetic height at LEO altitudes.
- **Atmosphere.** Density from the US Standard Atmosphere 1976, Table I, 100 to 1000 km (401 rows, as printed), interpolated exponentially between rows; outside that range the run stops rather than guessing. It is one fixed profile, with no solar-cycle, geomagnetic, latitude or time-of-day dependence; how it was read and checked, and what it cannot say, is in [ATMOSPHERE.md](ATMOSPHERE.md). The air co-rotates with Earth, so drag uses velocity relative to it.
- **Drag.** `a = -1/2 * rho * (Cd * A / m) * |v_rel| * v_rel`.
- **Thrust.** Along the velocity vector; magnitude `min(tau * P, eta_c * rho * v_rel * A_in * c_e)`: power-limited or intake-limited, whichever is smaller. It is a gross thrust: the momentum of the air the intake collects (ram drag) is inside `C_D * A`, not subtracted from the thrust. See [ABEP_MODEL.md](ABEP_MODEL.md).
- **Power.** Sunlight and eclipse from a cylindrical Earth shadow (no penumbra), with the Sun direction fixed in the inertial frame; a solar array given as a power, or as an area and an efficiency, with `S` = 1361 W/m^2; an ideal battery; and the orbit-averaged thruster duty from the energy budget.
- **Modes.** A deterministic state machine (SAFE, CHARGE, COAST, THRUST, FAULT) driven by power (battery charge and sunlight), density and thermal flags, and an external fault flag. At most one transition per control step; the thresholds are operating policy, not physics, and the thermal node behind the thermal flag has no sourced numbers. The rules, the thresholds and what the mission run does with them are in [FLIGHT_MODES.md](FLIGHT_MODES.md).

## Not in scope for v0.1

Attitude dynamics. Plasma or thruster physics. Density variation with solar and geomagnetic activity, which moves density at orbital altitude by about an order of magnitude or more, so absolute decay times are only indicative. Eccentric-orbit validation. Anything that needs an ephemeris: the Sun does not move over a run. The penumbra. Ram drag of the collected air as a separate term.

## Constants

Pinned, with sources and what has been checked, in [CONSTANTS.md](CONSTANTS.md): Earth's gravitational parameter, equatorial radius, flattening, rotation rate, J2 and the solar constant. The atmosphere is pinned in [ATMOSPHERE.md](ATMOSPHERE.md), and the sources of the ABEP model's parameters in [ABEP_MODEL.md](ABEP_MODEL.md).

## Validation

See [VALIDATION.md](VALIDATION.md). In short: analytic checks first (circular-orbit period, J2 nodal precession), then a published drag-versus-altitude curve for a 1 m^2 satellite with Cd near 2, then at least three published results including ESA's GOCE mission and published ABEP studies. If the physics does not match published curves, simplify before adding features.
