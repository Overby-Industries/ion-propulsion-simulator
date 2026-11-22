# Parameter sweeps

The question this simulator exists for, how much electrical power an air-breathing thruster needs to hold a satellite's altitude, depends on everything about the satellite and the thruster. A sweep runs the same mission over a grid of settings and writes one row of results for each point, so that the dependence can be read off a table or a plot.

`abep_sweep` is the program, `include/abep/sweep.hpp` is the code behind it. Every point is an ordinary run: the one `abep_run` does, with the same code (`simulate()` in `include/abep/full_run.hpp`), so a row holds the numbers a single run of those settings gives, character for character. A test checks that. A sweep is as deterministic as a run is.

## Run it

```bash
abep_sweep altitude_km=160:300:8 thrust_density_mn_per_kw=10,12,14 out=hold_power.csv
```

A setting given one number is fixed for the whole sweep, as in `abep_run` (`mass_kg=150`). A setting given `start:stop:count` takes `count` values equally spaced from `start` to `stop`, both included (`altitude_km=160:300:8` is 160, 180, ... 300; a count of 1 is just `start`). A setting given a list takes those values (`thrust_density_mn_per_kw=10,12,14`). The grid is every combination, the last swept setting changing fastest, so the order of the rows is fixed. `abep_sweep --help` lists the settings, which are the 36 of `abep_run` ([OUTPUT.md](OUTPUT.md)). Without `out=` the table goes to the standard output (on Windows, in text mode, with CR LF line ends; a file written with `out=` has line feeds only).

At most 100,000 points; a sweep with more is refused before it starts. Every point's settings are checked before the first run, so a point that makes no sense (a capture efficiency of 1.5) is found at once, and the error names the point and its values. Each row is written and flushed as it is made, so a long sweep can be watched in the file.

**The time to decay is part of every point.** It is the run that takes the time: the thruster is switched off and the satellite is flown down to the edge of the density table, which takes about a day at 200 km, six days at 250 km, and weeks to years higher up. `decay_max_years=0.5` ends that run at half a year; the row then says `decayed` 0 and `decay_time_days` is the length simulated. For a sweep of the hold power alone, a small `duration_days` and `decay_max_years` make it fast.

## The table

The first columns are the swept settings, in the order given, in the units of `abep_run`. Then these, which are rows of `summary.csv` ([OUTPUT.md](OUTPUT.md) says what each is), written as that file writes them, with 17 digits so that they read back exactly. A column is empty where the run has no such row.

| Column | Meaning |
|---|---|
| `end_reason` | why the mission run ended: `Completed`, or the altitude left the density table |
| `thrust_duty` | the share of the run spent thrusting |
| `final_altitude_km` | the altitude at the end of the mission run |
| `decayed`, `decay_time_days` | whether the satellite, with the thruster off, reached the decay altitude within `decay_max_years`, and the time that took (or the time simulated) |
| `hold_status` | `Holds` if some thruster power cancels the orbit-averaged drag, `IntakeLimited` if the intake cannot collect enough air at any power |
| `hold_power_w` | the thruster's electrical power, thrusting all the time, that makes the thrust over one orbit equal the drag over it (`inf` if `IntakeLimited`) |
| `hold_mean_drag_n`, `hold_mean_intake_limit_n` | the drag, and the most thrust the intake can support, averaged over one orbit |
| `hold_array_power_w` | the solar array's output in sunlight that carries `hold_power_w` and the bus load round the whole orbit, eclipse included, with an ideal battery that is big enough |

## What a sweep shows

Both tables are from the default satellite (100 kg, 1 m^2 frontal area, C_D 2.2, an intake of 2 m^2 with a capture efficiency of 0.46 and an exhaust velocity of 20 km/s, in an orbit at 51.6 degrees), in the 1976 standard atmosphere, so **they are illustrations of the model and not predictions for any satellite**: the defaults are illustrative, the thrust density is an assumption, and the atmosphere is one fixed profile ([LIMITATIONS.md](LIMITATIONS.md)).

**The power rises steeply as the orbit goes down.** `abep_sweep altitude_km=160:300:8 thrust_density_mn_per_kw=10,12,14 duration_days=0.1 decay_max_years=0.05`:

| Altitude, km | Mean drag, mN | Hold power at 10 mN/kW | at 12 | at 14 | Array output at 14 mN/kW |
|---:|---:|---:|---:|---:|---:|
| 160 | 102.8 | 10.3 kW | 8.57 kW | 7.34 kW | 13.3 kW |
| 180 | 36.0 | 3.60 kW | 3.00 kW | 2.57 kW | 4.94 kW |
| 200 | 16.7 | 1.67 kW | 1.39 kW | 1.19 kW | 2.56 kW |
| 220 | 8.76 | 876 W | 730 W | 626 W | 1.57 kW |
| 240 | 4.97 | 497 W | 414 W | 355 W | 1.10 kW |
| 260 | 2.97 | 297 W | 247 W | 212 W | 858 W |
| 280 | 1.85 | 185 W | 154 W | 132 W | 719 W |
| 300 | 1.18 | 118 W | 99 W | 84 W | 638 W |

The hold power is the mean drag over the thrust density, so it goes as the inverse of the thrust density (the columns differ by exactly 14/10 and 12/10): the thrust density is the number to know. The array output is more than that power, because the bus load (300 W) and the eclipse (the battery is charged in sunlight for the dark part of the orbit) are carried too: above about 250 km it is the bus load and the eclipse, much more than the thruster, that size the array.

**Whether it can hold at all is a matter of the intake, and does not depend on the altitude.** The thrust the intake can support is `rho * v * A_in * eta_c * c_e` and the drag is `1/2 * rho * v^2 * C_D * A_f`, so their ratio is `2 * A_in * eta_c * c_e / (C_D * A_f * v)`, with no density in it. `abep_sweep altitude_km=160:300:3 capture_efficiency=0.1,0.2,0.3,0.46 duration_days=0.1 decay_max_years=0.05` (13.7 mN/kW):

| Capture efficiency | Intake limit over drag (at 160 km; within 1% at the others) | Holds? |
|---:|---:|---|
| 0.10 | 0.48 | `IntakeLimited` at 160, 230 and 300 km: no power helps |
| 0.20 | 0.97 | `IntakeLimited` at all three, just short |
| 0.30 | 1.45 | `Holds` at all three |
| 0.46 | 2.22 | `Holds` at all three |

(At 0.30 the power at 160 km is 7.73 kW and at 0.46 it is 7.50 kW: the intake limit is nearer the drag, and the denser air near the bottom of the orbit makes it bind for part of the orbit. At 230 and 300 km the two are the same.) The altitude sets how much power is needed; the intake, the exhaust velocity and the frontal area set whether any power is enough. That is the part of the question that does not depend on the atmosphere.

## Limits

This is the default atmosphere and one satellite model at a time; a sweep cannot vary the atmosphere. It runs on one thread, one point after another. The Python module has no sweep function; a loop over `abep.run` does the same ([OUTPUT.md](OUTPUT.md)).

Tests: `tests/test_sweep.cpp` (the values of an axis and what is refused; the grid and the order of its points; that each row is the single run's own numbers; that the hold power is the drag over the thrust density to 1e-9 and falls with altitude; that a point that makes no sense is refused with its name before any run) and three `ctest` runs of the program ([VALIDATION.md](VALIDATION.md), case 29).
