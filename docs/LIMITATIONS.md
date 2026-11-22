# Limitations

What this simulator is, and is not, good for. The short version is in the README (Limits); this is the long one, with what has been checked for each limit and what has not. [VALIDATION.md](VALIDATION.md) has the checks themselves.

**It answers one question, as a sensitivity.** *If* a thruster produced so much thrust per watt, and the intake caught so much of the air, how much electrical power would it take to hold a satellite's altitude in low Earth orbit? It does not say that any thruster does. Read a result as "if the thrust density were X, the power would be Y", never as a prediction.

## The thruster

- **Thrust is a parameter, not a prediction.** The thrust is the electrical power times an assumed thrust density, and no more than the intake can feed: air collected (`rho * v * A_in * eta_c`) times an assumed exhaust velocity. The default of 13.7 mN/kW lies in the 10 to 14 mN/kW range of a JAXA concept as a secondary source quotes it (VALIDATION, finding 12); the primary paper is unread. To our knowledge ground tests have not yet shown net positive thrust from an air-breathing thruster at these conditions.
- **The intake is two numbers.** A capture efficiency and an area; no intake geometry, no compression, no dependence on altitude or on the species of the air (the mix changes with altitude, and atomic oxygen dominates its high end, as Romano et al. note). Real intake efficiency is unproven.
- **Ram drag of the collected air is not modelled.** The air the thruster takes in arrives at the speed of the satellite and leaves at the exhaust velocity; the thrust here is the gross thrust. If the exhaust velocity were not far above the orbital speed, the net thrust could be small or negative ([ABEP_MODEL.md](ABEP_MODEL.md)).
- **The exhaust velocity has no sourced range**; it matters only to the intake limit. The thrust density and the exhaust velocity are checked against each other (their product cannot imply an efficiency above 100%) and nothing else.

## The orbit and the air

- **Five fixed atmospheres, and none of them varies.** The 1976 US Standard Atmosphere (the default), a single profile for moderate solar activity, or one of four profiles for low, moderate, high long-term and high short-term solar and geomagnetic activity from ECSS-E-ST-10-04C (the JB-2006 model), chosen with the `atmosphere` setting ([ATMOSPHERE.md](ATMOSPHERE.md)). Each is an average over the day and the seasons at the equator: none depends on latitude, local time, season or the date. The density at these altitudes varies by a large factor with the solar cycle, which the four profiles bracket (high long term over low is 1.8 times at 160 km, 2.8 at 200 km, 4.9 at 250 km and 8.5 at 300 km), and by tens of percent with the rest. The ECSS standard puts the uncertainty of JB-2006 at 10% to 15% at mean activity, and up to 100% locally at 400 to 500 km, and says the model is valid from 120 km; its tables here start at 100 km. Decay times and hold powers go with the density, so absolute numbers are indicative. Checked: the standard atmosphere gives 6% to 33% more drag than the NRLMSISE-00 curves of Romano et al. (finding 11), and the moderate profile 27% to 51% more (finding 27). ESA's number for GOCE's unpowered decay near 280 km in the solar minimum of April 2009 is about two-thirds of what the 1976 standard gives (case 25: 280 m/day against 190); it lies between what the low and the moderate profile give, 95 and 390 m/day, with the low one 2.01 times under it, just outside the factor of 2 written for it (case 34, finding 25). A sweep can vary the atmosphere (`atmosphere=0:4:5`).
- **Gravity is a point mass plus J2.** No higher harmonics, no Sun or Moon, no radiation pressure, no tides. Checked against the analytic rate of the node (cases 2 and 2b), to the second-order term J2 leaves out.
- **The drag is `1/2 rho v^2 C_D A`, with a fixed C_D (2.2) and a fixed frontal area**, against air that turns with the Earth and has no winds. No attitude dynamics: the satellite always presents its frontal area. A real C_D depends on the altitude, the surface and the species of the air; GOCE's own was not available to check.
- **The density table ends at 100 km and at 1000 km**; a run that leaves it stops.

## Power and flight modes

- **The shadow is a cylinder**, with no penumbra, and the Sun is fixed in the inertial frame (no epoch, no seasons).
- **The array gives a fixed output in sunlight and the battery is ideal**: it has limits and no losses, no aging, no temperature. The bus load is constant.
- **The thresholds of the flight state machine are operating policy**, and the coefficients of the thruster's thermal node (heat capacity, conductance, temperatures) have no source at all. The state machine cannot see an eclipse coming ([FLIGHT_MODES.md](FLIGHT_MODES.md)).
- **The time to decay and the power to hold the altitude have this repository's own definitions** ([OUTPUT.md](OUTPUT.md)): decay to the lower edge of the density table with the thruster off; the thruster power, thrusting all the time, that makes the thrust over one orbit equal the drag over it. The hold is an average over one orbit, not a secular analysis.

## The numerics

A fixed-step fourth-order Runge-Kutta step (10 s by default). The time to decay is found to within a step. Results are deterministic and, on the compilers and settings tried (GCC, Clang, MSVC, with and without fused multiply-add), agree to the rounding of a double; bit-for-bit equality across compilers is not promised, and no test relies on it (findings 14 and 17).

## What has been checked, and how far

- Against analytic answers: the circular-orbit period, the nodal precession, the decay equation, the hold power's closed forms (cases 1, 2, 19, 20).
- Against **published curves**: the drag and the collectable mass flow of Romano et al. (2018), to 0.6% and 0.3%, in the paper's own atmosphere (cases 3 and 3b). That is a check of the formulas and the units, with the atmosphere taken from the paper.
- Against **published design points and numbers**, which are weaker: the drag the model finds lies inside the thrust ranges that ESA's GOCE and an ESA ABEP demonstrator were designed with (case 26), and GOCE's unpowered decay is within a factor of 1.5 of ESA's figure (case 25); the quoted powers of one study are matched by the thrust density's definition (case 27). **One quoted result is not matched**: the powers quoted for a JAXA concept are 7 to 10 times lower than the model's, for reasons that cannot be settled from a secondary source (case 28, finding 23).
- Against **the standard's own tables**: the four solar-activity profiles against the printed pages of ECSS-E-ST-10-04C and against its second model, NRLMSISE-00 (cases 30 and 32), the interpolation of their 20 km rows (case 31), and the setting that selects them (case 33). Their JB-2006 numbers are the standard's, not this repository's: the model is as good as JB-2006 is.
- Most of the published numbers were read second-hand, in the review of Romano et al.; the primary papers of the JAXA, ESA and Pekker and Keidar studies have not been read. There is **no flight data of an air-breathing thruster** to compare with, so the model has not been, and cannot yet be, validated as a *propulsion* model; it is checked as drag, orbit and power bookkeeping.

## Tools and platforms

The C++ library and its tests need a C++17 compiler and nothing else; built and tested with GCC, Clang (Linux) and MSVC (Windows). The Python module has been built and tested on Linux only, and the CI does not build it. The orbit view (Godot) is optional, was tested by hand on one Windows PC and GPU, and draws a stylised Earth, not real geography; the CI does not run it.
