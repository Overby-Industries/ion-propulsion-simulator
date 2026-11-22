# Output: the CSV files, the two summaries and the command-line program

A run gives three files, written by the command-line program `abep_run`, by the Python module, or by the functions in `include/abep/csv.hpp`:

| File | One row per | What it is |
|---|---|---|
| `samples.csv` | Kept step | The satellite and its systems over time: altitude, density, drag, thrust, power, battery, temperature, mode |
| `orbits.csv` | Orbit | The thrust duty and the energy balance of each orbit |
| `summary.csv` | Value | `key,value`: the totals of the run and the two summary outputs, the time to decay and the power to hold the altitude |

Everything here is a sensitivity tool's output, not a prediction. The thrust is power times an assumed thrust density, limited by an assumed intake, in one fixed standard atmosphere ([the README's limits](../README.md#limits)). The defaults of the program are illustrative: they are not data about any real satellite or thruster.

## Run it

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/abep_run                                   # the defaults, files into ./abep_out
./build/abep_run altitude_km=300 mass_kg=150 duration_days=3 out=my_run
./build/abep_run --help                            # every setting, its default and its meaning
```

Each argument is `name=value`; `out=` names the directory. A name that is not a setting, a value that is not a number, or a value that makes no sense stops the run with a message that names it (exit code 2). The program prints a short summary and the directory it wrote to. `ctest` runs it once, for a short run, so that it is built and exercised on every platform the tests are.

### The settings

All are numbers; an on/off setting is 0 or 1. They are the fields of `RunSettings` in `include/abep/run_config.hpp`, which builds the `Satellite`, the `Scenario` and the `ControllerConfig` that `run_mission()` takes. The orbit is circular, with the Sun fixed along +x, so the beta angle follows from the inclination and the node. The defaults of the thresholds are the state machine's own ([FLIGHT_MODES.md](FLIGHT_MODES.md)); the thermal node is off unless a heat capacity is given, because its numbers have no source.

| Setting | Default | Meaning |
|---|---|---|
| `altitude_km` | 250 | circular orbit, altitude above the equatorial radius, km |
| `inclination_deg` | 51.6 | orbit inclination, deg |
| `raan_deg` | 30 | right ascension of the ascending node, deg (the Sun is along +x) |
| `mass_kg` | 100 | satellite mass, kg |
| `frontal_area_m2` | 1 | frontal area, m^2 |
| `drag_coefficient` | 2.2 | drag coefficient |
| `thrust_density_mn_per_kw` | 13.7 | thrust per electrical power, mN/kW |
| `exhaust_velocity_km_s` | 20 | exhaust velocity, km/s (no sourced range; see [ABEP_MODEL.md](ABEP_MODEL.md)) |
| `intake_area_m2` | 2 | intake area, m^2 |
| `capture_efficiency` | 0.46 | share of the air swept up that reaches the thruster, 0 to 1 |
| `thruster_power_w` | 1000 | electrical power offered to the thruster in THRUST, W |
| `array_power_w` | 1500 | solar array output in sunlight, W |
| `bus_load_w` | 300 | housekeeping load, W |
| `safe_load_w` | 100 | housekeeping load in SAFE and FAULT, W |
| `battery_capacity_wh` | 800 | battery capacity, Wh |
| `battery_initial_wh` | 600 | battery energy at the start, Wh |
| `thermal_heat_capacity_j_per_k` | 0 | thruster heat capacity, J/K (0: no thermal node) |
| `thermal_conductance_w_per_k` | 5 | heat lost to the sink per kelvin, W/K |
| `thermal_sink_k` | 300 | sink temperature, K (also the temperature at the start) |
| `temperature_resume_k` | 0 | thrusting may resume at or below this, K (0: no limit) |
| `temperature_limit_k` | 0 | thrusting stops at or above this, K (0: no limit) |
| `temperature_fault_k` | 0 | a fault at or above this, K (0: no limit) |
| `soc_safe_enter` | 0.1 | battery fraction below which SAFE is entered |
| `soc_safe_exit` | 0.3 | battery fraction at which SAFE is left |
| `soc_thrust_stop` | 0.2 | battery fraction below which thrusting stops |
| `soc_thrust_start` | 0.5 | battery fraction at which thrusting may start or restart |
| `thrust_in_eclipse` | 0 | 1: thrust in eclipse too, 0: in sunlight only |
| `fault_hold_s` | 600 | time FAULT holds after the fault clears, s |
| `duration_days` | 1 | length of the run, days |
| `step_s` | 10 | time step, s |
| `sample_every` | 6 | keep one sample in this many steps (a whole number) |
| `include_j2` | 1 | 1: gravity with J2, 0: point mass |
| `corotating_atmosphere` | 1 | 1: the air turns with the Earth, 0: it does not |
| `thruster_enabled` | 1 | 0: a drag-only run |
| `decay_altitude_km` | 0 | decay altitude for the decay time, km (0: the lower edge of the density table, 100 km) |
| `decay_max_years` | 10 | longest time to simulate for the decay time, years |

Array outages and fault windows, which `run_mission()` supports, are not settings: they take a list of time windows, so they are available from C++ only.

## `samples.csv`

One header row, then one row per kept step (`sample_every` steps apart). Each row is the state at the **start** of the step and what was done during it.

| Column | Unit | Meaning |
|---|---|---|
| `t_s` | s | time since the start of the run |
| `mode` | | SAFE, CHARGE, COAST, THRUST or FAULT ([FLIGHT_MODES.md](FLIGHT_MODES.md)) |
| `altitude_km` | km | above the WGS 84 ellipsoid, along the geocentric radius |
| `density_kg_m3` | kg/m^3 | the 1976 standard atmosphere at that altitude |
| `relative_speed_m_s` | m/s | speed relative to the air, which turns with the Earth unless `corotating_atmosphere=0` |
| `drag_n` | N | 1/2 rho v_rel^2 Cd A |
| `thrust_n` | N | gross thrust, along the velocity |
| `net_drag_n` | N | `drag_n - thrust_n`: positive decelerates the satellite |
| `thruster_power_w` | W | electrical power the thruster drew |
| `generation_w` | W | solar array output (zero in shadow and during an outage) |
| `load_w` | W | housekeeping load the mode implies |
| `battery_wh` | Wh | stored energy |
| `temperature_k` | K | the thruster's temperature (constant if there is no thermal node) |
| `sunlit` | 1 or 0 | the geometry: not in Earth's shadow. The array can still be out |
| `x_km`, `y_km`, `z_km` | km | the satellite's inertial position, Earth-centred, in the frame of the initial state. In `abep_run` the Sun is along +x, and z is the Earth's axis, northwards. Last, so a reader that counts columns from the left is not disturbed. The orbit view reads them |

## `orbits.csv`

One row per orbit, an orbit being one two-body period of the initial orbit; the last one may be partial.

| Column | Unit | Meaning |
|---|---|---|
| `orbit` | | 0, 1, 2, ... |
| `duration_s` | s | time covered |
| `thrust_s` | s | time in THRUST |
| `duty` | | `thrust_s / duration_s`: the thrust duty cycle of the orbit |
| `eclipse_s` | s | time in Earth's shadow |
| `generated_wh`, `load_wh`, `thruster_wh` | Wh | energy generated, used by the housekeeping load, used by the thruster |
| `battery_change_wh` | Wh | what the battery gained (or lost, if negative) |
| `spilled_wh` | Wh | energy the battery could not take (positive: it was full) or could not give (negative: it was empty) |

The books balance by construction: `generated_wh - load_wh - thruster_wh = battery_change_wh + spilled_wh`, and the tests check it on propagated orbits ([VALIDATION.md](VALIDATION.md), cases 11 and 14).

## `summary.csv`

`key,value` rows, in this order.

| Keys | Meaning |
|---|---|
| `elapsed_s`, `end_reason`, `orbits` | how long the run lasted, why it ended (`Completed`, `AltitudeOutOfRange`, `BelowStopAltitude`) and how many orbits it covered |
| `thrust_duty` | the share of the run spent in THRUST |
| `seconds_in_SAFE`, `seconds_in_CHARGE`, `seconds_in_COAST`, `seconds_in_THRUST`, `seconds_in_FAULT` | time in each mode |
| `final_altitude_km`, `final_battery_wh`, `final_temperature_k` | where the run ended |
| `decayed`, `decay_time_s`, `decay_time_days`, `decay_final_altitude_km` | the time to decay with the thruster off, below |
| `hold_status`, `hold_power_w`, `hold_mean_drag_n`, `hold_mean_intake_limit_n`, `hold_sunlit_fraction`, `hold_array_power_w` | the power to hold the altitude, below. Left out if the satellite leaves the density table within one orbit |

## The two summary outputs

Both are in `include/abep/summary.hpp`, are built on `run_mission()` so they use the same orbit, drag, atmosphere and thrust models as everything else, and are deterministic.

### Time to decay with the thruster off

`time_to_decay(satellite, scenario, atmosphere, decay_altitude_m, max_duration_s)` propagates the scenario's initial orbit with the thruster off, with the scenario's step, J2 and rotating air, until the altitude above the ellipsoid falls below the decay altitude. It reports whether that happened (`decayed`), when (`time_s`), and the altitude it ended at. If the satellite is still above after `max_duration_s` (10 years by default) it says so: `decayed` is 0 and `time_s` is the longest time simulated.

- A decay altitude of 0, or one below the density table, means the table's lower edge, 100 km. The run has to stop there anyway: there is no density below it, and it is not invented.
- The time is found to within one step. The altitude is checked at the start of each step, which makes it late, and the run also stops, early, if a step's own stages reach below the table. At 100 km a satellite like the default one falls several km in a step, so `decay_final_altitude_km` can be that much above 100.
- It is as good as the atmosphere. The table is one fixed profile for moderate solar activity; the density at orbital altitude varies by about an order of magnitude or more with solar and geomagnetic activity, and the decay time varies about as much. Read it as a sensitivity: what the time would be if the air were this thick.
- It agrees with the circular-orbit decay equation `da/dt = -(Cd A / m) rho sqrt(GM a)` integrated separately, to 0.084% for an exponential atmosphere from 200 km to 120 km ([VALIDATION.md](VALIDATION.md), case 19).

### Power to hold the altitude

`power_to_hold_altitude(satellite, scenario, atmosphere)` is the electrical power of the thruster, thrusting all the time, that makes the thrust averaged over one orbit equal the drag averaged over the same orbit.

1. The initial orbit is propagated for one two-body period with the thruster off (J2, rotating air and the scenario's step, as set), so that the density and the relative speed at each step are the mission run's own.
2. The mean drag is the time average of `drag_n`. At each step the intake supports at most `cap = eta_c rho v_rel A_in c_e` of thrust (the thrust model's propellant limit); the thrust at power `P` is `min(tau P, cap)`.
3. The power is the `P` for which the time average of `min(tau P, cap)` equals the mean drag. That sum is piecewise linear in `P`, so it is solved exactly, by taking the caps in increasing order, not iterated.
4. If the average of the caps is below the mean drag, no power holds the altitude: `hold_status` is `IntakeLimited` and the power is infinite. The ratio of the thrust the intake supports to the drag is `eta_c A_in c_e / (1/2 Cd A v_rel)`, with no density in it, so a thicker atmosphere does not help.
5. `hold_array_power_w` is what that asks of the solar array: `(hold_power_w + bus load) / sunlit fraction`, the output in sunlight that carries the thruster and the housekeeping load round the whole orbit with an ideal battery that is big enough, the thruster running in eclipse too.

What it leaves out: the battery and the state machine (the mission run answers what a given array, battery and policy can do; this says what the thruster needs), the thruster's rated power (compare `hold_power_w` with it), and the angle between the velocity relative to the air, which the drag follows, and the inertial velocity, which the thrust follows (a few degrees at most, a cosine within 0.2% of 1). It holds the altitude on average: the altitude of an inclined orbit still varies round it, because the ellipsoid does.

It was checked by running the mission at that power: ten orbits at 300 km in the standard atmosphere, where drag alone lowers the semi-major axis by 1048 m, change by +1.0 m at the held power, -104 m at 90% of it and +106 m at 110% of it ([VALIDATION.md](VALIDATION.md), case 20).

## Number format and determinism

Numbers are written with 17 significant digits by default, so each reads back as the same `double`; the C++ functions take a smaller number of digits (1 to 17). The decimal point is a point and there is no digit grouping, whatever the locale of the program or of the stream; the stream's own settings are put back afterwards. A value that is not finite is written as the stream writes it (`inf`, `nan`). Keys and values of `summary.csv` never contain a comma, a quote or a line break.

The simulator is deterministic, and the same inputs give the same CSV on the same build, but not necessarily byte for byte across compilers and processors: where a compiler fuses a multiply and an add, a last digit differs ([VALIDATION.md](VALIDATION.md), findings 14 and 17). Compare outputs with a tolerance, not with a checksum.

## From C++

```cpp
#include "abep/csv.hpp"
#include "abep/run_config.hpp"
#include "abep/standard_atmosphere_1976.hpp"
#include "abep/summary.hpp"

abep::RunSettings settings;                        // the defaults above
abep::set_setting(settings, "altitude_km", 300.0);
const abep::Mission m = abep::make_mission(settings);
const abep::DensityTable air = abep::ussa1976::density_table();

const abep::MissionResult run = abep::run_mission(m.satellite, m.scenario, m.controller, air);
const abep::DecayResult decay = abep::time_to_decay(m.satellite, m.scenario, air);
const abep::HoldResult hold = abep::power_to_hold_altitude(m.satellite, m.scenario, air);

abep::write_samples_csv(std::cout, run.samples);   // or any std::ostream
abep::write_orbits_csv(std::cout, run.orbits);
```

`Satellite`, `Scenario` and `ControllerConfig` can also be filled in directly, which is the only way to give array outages and fault windows. `abep::simulate(settings, air)` in `include/abep/full_run.hpp` does all of the above in one call, `summary_rows()` gives the rows of `summary.csv` and `write_run_files()` writes the three files; it is what `abep_run` and the Python module call, so they give the same numbers.

## From Python

The module `abep` runs the same simulation from Python and gives the results as plain dicts and lists, so `pandas.DataFrame(result["samples"])` and matplotlib work with nothing else installed. It needs pybind11 to build, and it is off by default, so that the library and its tests need only a C++17 compiler (CMake 3.18 or newer for the module):

```bash
python -m pip install pybind11                  # or your distribution's pybind11 package
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DABEP_BUILD_PYTHON=ON      # add -Dpybind11_DIR=$(python -m pybind11 --cmakedir) if CMake does not find it
cmake --build build
PYTHONPATH=build python -c "import abep; print(abep.settings()['altitude_km'])"
```

The module is the file `abep.*.so` (`abep.*.pyd` on Windows) in the build folder, and `ctest` runs `python/test_abep.py` against it.

```python
import abep
r = abep.run(altitude_km=300, mass_kg=150, duration_days=2)   # the settings above, as keywords
r["samples"]["altitude_km"]         # a list per column of samples.csv
r["orbits"]["duty"]                 # a list per column of orbits.csv
r["summary"]["hold_power_w"]        # the keys of summary.csv: numbers, or text for end_reason and hold_status
abep.write_csv("my_run", altitude_km=300)   # the three CSV files, as abep_run writes them
abep.settings()                     # every setting and its default
```

A setting that does not exist, or a value that makes no sense, raises `ValueError`, naming the setting; a value that is not a number raises `TypeError`. `True` and `False` count as 1 and 0. The simulation runs without the interpreter's lock, so other Python threads go on meanwhile. The columns hold the numbers of the CSV files exactly (the files have 17 digits), and the same inputs give the same lists.

It has been built and tested with GCC 16.2 and Python 3.14 on Linux, with pybind11 3.0.1. It has not been built on Windows or macOS, and the CI does not build it.

## Many runs: sweeps

`abep_sweep` runs the same mission over a grid of settings (`altitude_km=160:300:8 thrust_density_mn_per_kw=10,12,14`) and writes one table with a row per point: the swept settings, then the rows of `summary.csv` that matter for the hold power. Every row is what a single `abep_run` of those settings gives. [SWEEPS.md](SWEEPS.md) has the syntax, the columns and two worked tables.

## The orbit view

[`viewer/`](../viewer/README.md) is a small Godot program that plays a run back as a picture: the orbit coloured by flight mode, the satellite with a plume while it thrusts, the Earth's shadow, and the numbers of the moment. It reads `samples.csv`, and needs these of its columns: `t_s`, `mode`, `altitude_km`, `drag_n`, `thrust_n`, `net_drag_n`, `thruster_power_w`, `battery_wh`, `temperature_k`, `sunlit`, `x_km`, `y_km` and `z_km`. A file that lacks one, or whose time does not increase, is refused with the reason. If an `orbits.csv` and a `summary.csv` are in the same folder it reads them too, for the orbit number and the numbers of the whole run. It draws the positions as they are written, in the frame of the initial state with the Sun along +x, with the Earth turning at 7.292115e-5 rad/s from the start; it computes nothing of its own. Nothing in the simulator depends on it.

## Not here yet

- **The orbit view anywhere but Windows.** It was written and tested with Godot 4.6.3 on Windows, by hand, with one GPU; the CI does not run it.
- **The module on Windows and macOS.** Nothing in it is specific to Linux, but it has not been tried.
