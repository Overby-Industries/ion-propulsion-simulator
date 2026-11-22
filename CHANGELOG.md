# Changelog

What changed in each release. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Before 1.0 the settings, the CSV columns and the C++ interfaces may change in any release.

## 0.1.0 (unreleased)

The first release. The simulator answers one question: how much electrical power an air-breathing electric thruster needs to hold a satellite's altitude in low Earth orbit, *if* it makes a given thrust per watt and its intake catches a given share of the air. It is a sensitivity tool, not a thruster model.

### Added

- **Orbit and drag.** A two-body plus J2 propagator (fixed-step RK4, deterministic), the altitude above the WGS 84 ellipsoid, drag against air that turns with the Earth, and the density of the 1976 US Standard Atmosphere from 100 to 1000 km, read from the printed table and checked ([ATMOSPHERE.md](docs/ATMOSPHERE.md)).
- **The ABEP model.** Thrust limited by power or by the intake, an eclipse model, a solar array and an ideal battery ([ABEP_MODEL.md](docs/ABEP_MODEL.md)).
- **A flight state machine** (SAFE, CHARGE, COAST, THRUST, FAULT) with a thruster thermal node, and a mission run that drives them through the orbit ([FLIGHT_MODES.md](docs/FLIGHT_MODES.md)).
- **Output.** CSV files of samples, orbits and a summary; two summaries (the time to decay with the thruster off, and the power to hold the altitude); the `abep_run` program; and a Python module (pybind11) that returns the same results as lists ([OUTPUT.md](docs/OUTPUT.md)).
- **Parameter sweeps.** `abep_sweep` runs the mission over a grid of settings and writes one row of results per point ([SWEEPS.md](docs/SWEEPS.md)).
- **An orbit view** in Godot 4.6 that plays the CSV files back ([viewer/](viewer/)), and a Windows download of it with `abep_run` on [itch.io](https://overby-industries.itch.io/ion-propulsion-simulator).
- **Validation.** Checks against analytic answers and against published results, 34 cases and 24 findings, failed first attempts included ([VALIDATION.md](docs/VALIDATION.md)); and the limits of the model written out ([LIMITATIONS.md](docs/LIMITATIONS.md)).
- **A technical note**, six pages: [TECHNICAL_NOTE.md](docs/TECHNICAL_NOTE.md) and its [PDF](docs/TECHNICAL_NOTE.pdf).

### Checked

- 5,158 automated checks, with GCC 16.2, GCC 13.4, Clang 21.1 and MSVC (`/W4`), 50 builds of the final tree in all, with no warnings.
- 105 defects injected one at a time into the flight, mission, output, position and sweep code; all caught.
- The drag and the intake mass flow agree with the curves of Romano et al. (2018) to within 0.6% and 0.3%, in the paper's atmosphere. GOCE's drag lies inside the thrust range ESA gives, and its unpowered decay near 280 km is 1.47 times ESA's figure.

### Known limits and open findings

- **One comparison fails.** The powers quoted for a JAXA concept (470 W at 180 km, 3.3 kW at 140 km) are not reproduced: the model needs 7 to 10 times as much, for a reason that cannot be settled from a secondary source (VALIDATION, finding 23).
- Thrust is a parameter. Ram drag, the intake's physics and a range for the exhaust velocity are not modelled or not sourced.
- One fixed atmosphere (the 1976 standard); density at low and high solar activity is not modelled.
- Most published numbers used are second-hand. The WGS 84 constants were confirmed from secondary sources.
- The Python module is tested on Linux only, and the orbit view is tested by hand, not in the CI.
