# Changelog

What changed in each release. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Before 1.0 the settings, the CSV columns and the C++ interfaces may change in any release.

## 0.2.0 (unreleased)

Module two, step one: the atmosphere at low, moderate and high solar activity, so that an answer can be a bracket and not one number.

### Added

- **Four solar-activity atmospheres** from ECSS-E-ST-10-04C Annex G (the JB-2006 total density, 100 to 900 km, for low, moderate, high long-term and high short-term activity), chosen with the new `atmosphere` setting: 0 is the 1976 standard, the default, and 1 to 4 are the new ones. There are 37 settings now. The data are in `data/ecss_e_st_10_04c_*.txt`, `tools/ecss_table.py` checks them and generates the header, and [ATMOSPHERE.md](docs/ATMOSPHERE.md) says where they come from and what they cannot tell you.
- **A monotone cubic interpolation** option for `DensityTable`, used by the 20 km rows of the new tables. The 1976 table keeps its log-linear rule, so every earlier result is unchanged.
- **`abep_sweep` can sweep the atmosphere** (`atmosphere=0:4:5`), and `write_sweep_csv` has a form without a table for it ([SWEEPS.md](docs/SWEEPS.md)).
- Validation cases 30 to 35 and findings 25 to 30 ([VALIDATION.md](docs/VALIDATION.md)).
- **A Run window in the orbit view.** **Run...** opens a window with the altitude, the mass, the frontal area, the thrust per power, the atmosphere and the length of the run, and a box for any other setting as `name=value`. **Run** starts `abep_run` (next to the viewer, in `build/`, or named by `ABEP_RUN`) as a process of its own, and plays the result from its start; the window shows `abep_run`'s own summary, or its own words when it refuses a setting, and **Stop** ends a run that takes long. `ABEPOrbitView -- altitude_km=300 mass_kg=150` does the same from the command line (`--then-quit` leaves when it is done). A test runs the window headless against a real `abep_run`, and the release script runs the exported viewer the same way ([viewer/README.md](viewer/README.md)).
- **`abep_run` asks, and starts the orbit view.** `abep_run --ask` asks for the altitude, the mass, the frontal area, the thrust per power, the atmosphere and the length of the run, one at a time, and takes any other setting as `name=value`; when the run is done it offers to show it in the orbit view, and `--view` does so without asking. The orbit view is `ABEPOrbitView.exe` next to `abep_run.exe` (where the Windows download keeps it) or the program that `ABEP_VIEWER` names ([OUTPUT.md](docs/OUTPUT.md)). A test feeds it answers on its standard input and checks what it starts (`tests/abep_run_ask.cmake`).

### Changed

- `abep_run.exe`, started by a double-click on Windows, asks its questions (`--ask`, above) and waits for Enter before its console window closes (the window used to flash by); from a command prompt or a script it neither asks nor waits, as before.
- `simulate(settings)` takes the atmosphere from the setting; `simulate(settings, table)` is as it was. `abep_run` names the atmosphere in its first line when it is not the default.

### Known limits

- Each profile is an average over the equator, the day and the seasons, and JB-2006 is valid from 120 km. The standard is copyright ESA for the members of ECSS; the numbers are reproduced with attribution and no licence for reuse was found in it.
- ESA's decay rate for GOCE lies between the low and the moderate profile's, and the low one misses the factor of 2 written for it, by 0.5% (finding 25).
- The profiles do not explain the JAXA gap (finding 28).

## 0.1.0 (2026-11-22)

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
