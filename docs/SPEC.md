# ABEP: Ion Thrusters Simulator -- specification

**Status:** draft. This describes what v0.1 will do. None of the model below is implemented yet.

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
| Thrust density | tau | mN/kW | Thrust per unit electrical power. The plan assumes a published range of 10 to 13.7 mN/kW; the source is to be cited before v0.1. Stored internally as N/W. |
| Thruster power | P_t | W | Electrical power drawn while thrusting. |
| Array power | P_a | W | Solar array output in sunlight. *(ABEP model)* |
| Battery energy | E_b | Wh | Usable energy for eclipse and thrusting. *(ABEP model)* |
| Bus load | P_l | W | Housekeeping load. *(ABEP model)* |
| Intake area, capture efficiency | A_i, eta_c | m^2, - | Limit thrust by the propellant the intake can collect. This is the least settled part of the model; the form of the limit is decided when it is written. *(ABEP model)* |

## Outputs

| Output | Unit | Notes |
|---|---|---|
| Net drag vs time | N | Drag minus thrust; positive decelerates the satellite. |
| Altitude vs time | km | |
| Thruster duty cycle | - | Fraction of time in THRUST, per orbit and over the whole run. |
| Mode timeline | - | SAFE, CHARGE, COAST, THRUST or FAULT. *(state machine)* |
| Summary | | Time to decay with the thruster off; steady-state power to hold altitude; energy balance per orbit. |

Results are written as CSV. Python bindings (pybind11) for plotting come later.

## Method (v0.1)

- **Frame and units.** Earth-centred inertial, SI. Gravity is a point mass plus the J2 term, which carries Earth's oblateness; no higher harmonics, no third bodies, no solar radiation pressure.
- **Integration.** Fixed-step RK4. Deterministic: no adaptive steps, no random numbers, no fast-math.
- **Altitude.** For the density lookup, the distance above the WGS 84 ellipsoid along the geocentric radius. Measuring against the equatorial radius instead would be wrong by up to 21 km at the poles (13 km at 51.6 deg latitude), which changes density by tens of percent; this form is within 5 m of true geodetic height at LEO altitudes.
- **Atmosphere.** Density interpolated, exponentially between nodes, from a tabulated static profile; outside the table's range the run stops rather than guessing. The intended table is the US Standard Atmosphere 1976, to be added once its values have been checked against the printed report (see [VALIDATION.md](VALIDATION.md), finding 4); no density data is included yet. The air co-rotates with Earth, so drag uses velocity relative to it.
- **Drag.** `a = -1/2 * rho * (Cd * A / m) * |v_rel| * v_rel`.
- **Thrust.** Along the velocity vector; magnitude from the ABEP model.
- **Power.** Sunlight and eclipse from a cylindrical Earth shadow.
- **Modes.** A deterministic state machine (SAFE, CHARGE, COAST, THRUST, FAULT) driven by power, density and thermal flags.

## Not in scope for v0.1

Attitude dynamics. Plasma or thruster physics. Density variation with solar and geomagnetic activity, which moves density at orbital altitude by about an order of magnitude or more, so absolute decay times are only indicative. Eccentric-orbit validation. Anything that needs an ephemeris.

## Constants

Pinned, with sources and what has been checked, in [CONSTANTS.md](CONSTANTS.md): Earth's gravitational parameter, equatorial radius, flattening, rotation rate and J2. The solar constant is added with its source when the power model is written.

## Validation

See [VALIDATION.md](VALIDATION.md). In short: analytic checks first (circular-orbit period, J2 nodal precession), then a published drag-versus-altitude curve for a 1 m^2 satellite with Cd near 2, then at least three published results including ESA's GOCE mission and published ABEP studies. If the physics does not match published curves, simplify before adding features.
