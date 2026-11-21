# Validation

What has been checked against what, and what has not. A model is only as trustworthy as this page. Cases are added before the code that needs them, and a case is only marked done when its test passes in CI.

| # | Case | Reference | Tolerance, set before running | Result | Status |
|---|---|---|---|---|---|
| 1 | Circular-orbit period, and an eccentric orbit (e = 0.3) | Analytic, two-body: `T = 2 * pi * sqrt(a^3 / mu)` | 1 mm and 1 um/s (circular), 1 cm and 10 um/s (eccentric), after one orbit with a 1 s step; 1e-12 relative drift in energy and angular momentum | Passes | Done |
| 2 | J2 nodal precession, prograde (400 km, 51.6 deg) | Analytic, first order in J2: `dOmega/dt = -3/2 * J2 * (Re/p)^2 * n * cos(i)` | 0.5% at Earth's J2, and the difference must shrink at least 8x at J2/10 | +0.434% at J2; +0.043% at J2/10, 10.0x smaller | Done |
| 2b | Retrograde orbit and the sun-synchronous rate (800 km, 98.6 deg) | `2 * pi / (365.2422 d)` = 0.98565 deg/day | 0.5% (formula against the sun-synchronous rate); 0.5% (propagated against the formula) | Formula 0.98530 deg/day (-0.04%); propagated 0.98950 (+0.43% against the formula) | Done |
| 2c | The node does not move without J2, or for a polar orbit | Analytic | 1e-9 rad and 1e-6 rad over 10 days | Passes | Done |
| 3 | Drag against altitude for a 1 m^2 satellite with Cd near 2 | A published drag-versus-altitude curve; source to be chosen and cited | | | Not started |
| 4 | Published results, at least three | ESA's GOCE mission and published ABEP studies; sources to be chosen and cited | | | Not started |
| 5 | Drag decay per revolution | Classical: `delta_a = -2 * pi * rho * (Cd * A / m) * a^2` (circular orbit, constant density, non-rotating air) | 0.1% | -0.0002% | Done |
| 6 | J2 derived from the WGS 84 defining parameters | A 60-digit evaluation, and the published normalized coefficient C2,0 | 1e-15 absolute | Passes | Done |
| 7 | Standard-atmosphere density table (US Standard Atmosphere 1976) | The printed 1976 tables | | | **Blocked**: see finding 4 |
| 8 | Altitude above the ellipsoid, for the density lookup | Points built on the ellipsoid; an independent iterative geodetic-height conversion | 1e-6 m (on or at a known height above the ellipsoid); 5 m against geodetic height over latitudes 0 to 90 deg and heights 200 to 800 km | Passes; the worst difference found while choosing the tolerance was 4.0 m | Done |

Cases 5, 6 and 8 were added during the propagator work, as the rules below allow, because they test parts of the model that cases 1 and 2 do not.

## Rules

- Every constant and every published number used in the code or in a test is cited here or in [`CONSTANTS.md`](CONSTANTS.md), with where it came from.
- Tolerances are written down before the comparison is run, not adjusted to make it pass.
- If a case fails, the finding is recorded here whether or not it is later fixed.

## Findings

1. **J2 derivation: first attempt failed.** The test first evaluated the textbook formula for `q0` directly. That subtracts two numbers near 36.5 to leave about 7e-5, losing about five digits, so the 1e-15 tolerance asked for more than the formula can give. The tolerance was kept and the evaluation fixed: `q0` is now summed from its series in `e'`, which has no cancellation. The stored literal also carried only 13 digits and was off by 3e-16; it is now the correctly rounded double.
2. **Conservation of h_z: first attempt failed.** With a 10 s step and a 1e-10 relative tolerance over a day, h_z drifted by 2.2e-10 (measured afterwards: 6.9e-12 at a 5 s step and 7.4e-14 at 2 s, falling about 32x per halving). That is RK4's own amplitude error, about `(n * dt)^6 / 144` per step, and says nothing about J2; the tolerance had been estimated carelessly. The test now uses a 2 s step with the same tolerance. The other J2 tests keep a 10 s step, where the same error amounts to about 2e-9 over their 10-day runs, far below what they measure.
3. **Nodal-rate gap is second order in J2.** The propagated rate differs from the first-order formula by +0.43%. To check that this is a neglected higher-order term and not a wrong coefficient, the run was repeated with J2 scaled down: the gap was +0.4345% at J2, +0.2170% at J2/2, +0.1085% at J2/4 and +0.0436% at J2/10. It is proportional to J2, as a second-order term must be. Case 2 now asserts this.
4. **No atmosphere data is shipped yet.** The only implementation of the 1976 standard found, the `ussa1976` package (0.3.4 on PyPI), does not compare itself against the printed tables in its own tests, and its densities are several percent above the values commonly quoted for the standard (for example 2.98e-12 kg/m^3 at 400 km against a quoted 2.80e-12; the quoted figures are recalled from memory and not yet checked, which is the point). A second package, `pyatmos`, could not be used here. Neither can stand in for the printed table. The source is the NOAA / NASA / USAF report *U.S. Standard Atmosphere, 1976* (NASA-TM-X-74335, [NTRS 19770009539](https://ntrs.nasa.gov/citations/19770009539)), whose main tables run to 1000 km. The density interpolation is written and tested (`include/abep/atmosphere.hpp`) against an exact exponential; the table itself is added once its values have been read from that report.
5. **Primary source for the WGS 84 parameters not yet read.** See [`CONSTANTS.md`](CONSTANTS.md).
6. **Altitude must be measured above the ellipsoid.** Taking altitude as `|r|` minus the equatorial radius is wrong by up to 21.4 km at the poles and 13.1 km at 51.6 deg latitude, enough to move density by tens of percent. Measured along the geocentric radius above the WGS 84 ellipsoid, it is within 4.0 m of true geodetic height over latitudes 0 to 90 deg and heights of 200, 400 and 800 km. That worst-case figure came from a throwaway numerical comparison, and `tests/test_orbit.cpp` now repeats the comparison with a 5 m tolerance.

## Known unknowns

- The thrust-density range assumed by the plan (10 to 13.7 mN/kW) is still to be traced to its published source.
- Real intake efficiency is unproven, so the capture-limited part of the thrust model has nothing to be validated against yet.
