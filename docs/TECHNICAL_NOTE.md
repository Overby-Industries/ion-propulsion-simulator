# How much power does an air-breathing thruster need to hold a low orbit?

*A technical note on ABEP, an open simulator. Overby Industries. Version 0.1, draft of 22 November 2026. Code, data and every number below: [github.com/Overby-Industries/ion-propulsion-simulator](https://github.com/Overby-Industries/ion-propulsion-simulator) (Apache 2.0).*

## Abstract

ABEP is a small open simulator, written in C++17 with no dependencies, for one question: *if* an atmosphere-breathing electric thruster made a given thrust per watt, and its intake caught a given share of the air, how much electrical power would it take to hold a satellite's altitude in low Earth orbit? It flies an orbit under point-mass gravity plus J2 and drag in the 1976 US Standard Atmosphere, with a solar array, eclipses, a battery and a five-mode flight state machine, and it reports the time to decay with the thruster off and the power to hold the altitude. It is a sensitivity tool, not a thruster model: thrust is an input.

The code is checked by 5,158 automated checks, by 105 deliberately injected defects (all caught), and by builds with GCC, Clang and MSVC. Against published numbers it reproduces the drag and intake-flow curves of Romano et al. (2018) to 0.6% and 0.3%, puts GOCE's drag inside the thrust range ESA gives and its unpowered decay within a factor of 1.5 of ESA's figure, and **does not reproduce** the powers quoted for a JAXA concept: it needs 7 to 10 times as much. That gap is unexplained. Section 8 lists what we would like criticised.

## 1. The question

For a satellite of given mass, frontal area and drag coefficient in a near-circular low orbit: how much electrical power must an air-breathing electric thruster have to cancel atmospheric drag, and what do altitude, net drag, thruster duty cycle and the energy budget look like over time, eclipses included?

The answer is a sensitivity: *if* the thrust per watt were X and the intake caught Y, the power would be Z. The simulator does not say that any thruster achieves X or Y. To our knowledge, ground tests have not yet shown net positive thrust from an air-breathing thruster at these conditions, and real intake efficiency is unproven.

The inputs are 36 named settings: altitude, inclination, mass, frontal area, drag coefficient, thrust density (mN/kW), exhaust velocity, intake area and capture efficiency, array power, battery energy, bus load, and the thresholds of the flight state machine. The results are three CSV files (`samples.csv`, `orbits.csv`, `summary.csv`), also returned as lists by a Python module. Two summaries answer the question directly:

- **Time to decay.** With the thruster off, the time to fall to the lower edge of the density table (100 km), found to within one step.
- **Power to hold the altitude.** The thruster's electrical power, thrusting all the time, that makes the thrust over one orbit equal the drag over it. The result is `Holds`, or `IntakeLimited` if the intake cannot collect enough air at any power. The solar-array output that carries that power and the bus load round a whole eclipsed orbit is reported too.

A second program, `abep_sweep`, runs the mission over a grid of settings and writes one row of results per point. An optional orbit view (Godot) plays the files back as a picture.

## 2. The model

All of it is header-only C++17 in `include/abep/`. The equations, their sources and what is left out are in [ABEP_MODEL.md](ABEP_MODEL.md); this is the short form.

- **Orbit.** An Earth-centred inertial frame in SI units. Gravity is a point mass plus J2, with J2 derived from the WGS 84 defining parameters. The integrator is a fixed-step fourth-order Runge-Kutta (10 s by default), with no random numbers and no fast-math. The altitude used for the density is the height above the WGS 84 ellipsoid along the geocentric radius, within 4.0 m of geodetic height; measured above the equatorial radius it would be wrong by up to 21 km at the poles, and the density by tens of percent.
- **Atmosphere.** Density from the 1976 US Standard Atmosphere, Table I, 100 to 1000 km (401 rows as printed), interpolated log-linearly, with air that turns with the Earth. We read the table from the printed report because the one implementation we found (the `ussa1976` package, version 0.3.4) is up to 6.7% above it; we did not find out why. All 401 rows agree with the report within its rounding. It is one fixed profile for moderate solar activity.
- **Drag.** `D = 1/2 * rho * v_rel^2 * C_D * A`, with `v_rel` the speed relative to the turning air.
- **Thrust.** `F = min(tau * P, mdot_c * c_e)`, where `mdot_c = eta_c * rho * v_rel * A_in` is the air the intake collects, `tau` the thrust density, `P` the electrical power and `c_e` the exhaust velocity: the thrust density times the power, or what the intake can feed, whichever is less. The intake bound is Eq. 9 of Romano et al. (2018); the cap by `tau * P` is ours. It is a *gross* thrust: the momentum of the incoming air (ram drag) stays inside `C_D * A` (Section 6). A thrust density and an exhaust velocity that would make the thruster more than 100% efficient (`tau * c_e > 2`) are refused.
- **A consequence.** The intake's thrust and the drag are both proportional to `rho`, so their ratio, `2 * eta_c * (A_in / A) * c_e / (C_D * v_rel)`, does not depend on altitude. *Whether* an intake can hold a satellite is independent of the atmosphere; *how much power* it takes is not (Section 5).
- **Power.** A cylindrical Earth shadow (the eclipse fraction in closed form, no penumbra), a Sun direction fixed in the inertial frame, a solar array with a fixed output in sunlight, and an ideal battery.
- **Flight state machine.** Five modes (SAFE, CHARGE, COAST, THRUST, FAULT), deterministic, at most one transition per control step, driven by battery charge, sunlight, density and a thruster temperature. The thresholds are operating policy and the thermal coefficients are assumptions: the checks show that the machine does what its rules say, not that the numbers are right.

## 3. How the code was checked

Cases are written down before the code that needs them, each with a tolerance set before the comparison is run, and a failed comparison is recorded as a finding whether or not it is later fixed. [VALIDATION.md](VALIDATION.md) holds the table of 34 cases and the 24 findings.

- **Against analytic answers.** The circular-orbit period (to 1 mm after an orbit). The nodal precession from J2: +0.43% from the first-order formula, shown to be a second-order term by scaling J2 (the gap falls 10.0 times at J2/10). The drag decay per revolution (-0.0002%). The time to decay in an exponential atmosphere against the integrated decay equation (+0.084%). The eclipse fraction against a propagated orbit (within 1.4e-4 of an orbit). The energy and thermal books of a run against closed forms.
- **5,158 automated checks**, run by `ctest` and, on every pull request, by the CI on Linux (GCC) and Windows (MSVC). The final tree was also built 50 ways: GCC 16.2, GCC 13.4 and Clang 21.1 at `-O0` to `-O3`, with and without fused multiply-add, with sanitizers and hardening, and MSVC at `/W4`, with no warnings. Results are deterministic and agree across compilers to the rounding of a double; bit-for-bit equality across compilers is not promised, and no test relies on it.
- **Injected defects.** Defects were put into the code one at a time (a threshold moved by one, a unit wrong, thrust reversed, two columns swapped) and the suite was run again each time: 105 of 105 were caught. One survived at first, the last value of a sweep range, and a sharper test was added. A few are caught by a single check each; the findings name those as the thinnest guards.
- **What the failures taught.** Four of the 24 findings. (1) The first comparison with a published drag curve failed by a constant 13%: the curve assumes that the air turns with the Earth, which the paper does not say, and the fit showed it (finding 10). (2) Two tolerances sat below floating-point rounding noise and failed the CI on another compiler, and a bit-for-bit comparison of two evaluations of one expression failed on Linux when the compiler fused multiply-adds differently (findings 9, 14 and 17). (3) The first flight state machine thrust itself into SAFE every orbit: the thrust duty of a propagated run was 0.534 against 0.502 from the closed-form power budget, with a tolerance of 1%, and no table of transitions could have shown it (finding 13). (4) The orbit view passed its tests from the first run and was still wrong in six ways that only looking at it found (finding 20).

## 4. Comparison with published numbers

Eight comparisons, each with its tolerance written before the run. The first three use curves read from the PDF's own vector drawings; the others use numbers from ESA's web pages and from the review in Romano et al., which are second-hand (the primary papers have not been read). The first column is the case number in [VALIDATION.md](VALIDATION.md).

| Case | Compared | Published | Model | Set beforehand | Result |
|---|---|---|---|---|---|
| 3 | Drag on 1 m², C_D 2.2, at 14 altitudes from 120 to 250 km, in the paper's atmosphere | Romano et al. 2018, Fig. 6 | worst difference 0.59% | 3% | agrees (needs air that turns with the Earth) |
| 3b | Collectable mass flow, capture efficiency 0.35, 0.46 and 0.90 | Fig. 5 | worst difference 0.30% | 3% | agrees |
| 3c | The same drag in the 1976 standard atmosphere | Fig. 6 | 1.06 to 1.33 times the paper's | 0.5 to 2 times (a sanity band) | the atmospheres differ; not a test of the model |
| 25 | GOCE's unpowered decay near 280 km | ESA: about 190 m/day | 280 m/day (1.47 times) | factor of 3 | inside; the model decays faster, as an extended solar minimum would make it |
| 26a | GOCE's drag, 1.1 m², at 224, 235, 254 and 265 km | thruster range 1 to 20 mN | 9.06, 6.67, 4.07 and 3.11 mN | inside the range | inside |
| 26b | ESA's ABEP demonstrator: 1000 kg, 1 m², 200 km, up to 1 kW, 2 to 20 mN | quoted in Romano et al. | drag 14.8 mN; 742 W to hold the orbit at 20 mN/kW | in 2 to 20 mN; at most 1 kW | inside |
| 27 | Pekker and Keidar: 22 N and 9.1 N at 13 mN/kW | 1.6 to 2 MW; 700 to 800 kW | 1.69 MW; 700 kW | inside the quoted | arithmetic only: tests the definition of thrust density |
| 28 | JAXA concept: 1.5 m², 10 to 14 mN/kW, at 180 and 140 km | 470 W; 3.3 kW | 3.49 to 4.88 kW; 22.2 to 31.0 kW | factor of 2 | **not met**: 7.4 to 10.4 times; 6.7 to 9.4 times |

What the table does and does not say. Cases 3 and 3b check the formulas and the units against a published curve, with the atmosphere taken from the paper; they say nothing about the thruster. Cases 25 to 27 are weaker: ranges and design points, mostly second-hand. Case 25 is a plausibility band: ESA's figure was for an extended solar minimum, and GOCE's own drag coefficient and effective area were not found, so how much of the 1.47 is the atmosphere cannot be told. There is no flight data of an air-breathing thruster, so the model has not been, and cannot yet be, validated as a *propulsion* model. It is checked as drag, orbit and power bookkeeping.

**The JAXA gap (finding 23).** The ratio is nearly the same at both altitudes, so the cause is a factor in the study's assumptions and not in how the density falls with altitude. A fit made after the fact, which is not an explanation: the drag on the study's *inlet* area (0.48 m², not the 1.5 m² frontal area) with C_D = 1 at 14 mN/kW comes to 507 W and 3.23 kW, which is 1.08 and 0.98 of the quoted values. Whether the study took its drag that way cannot be told from a secondary source: the primary paper has not been read, and neither its atmosphere nor its drag coefficient is known here. Until it is read, nothing in the repository should be taken to agree or disagree with the JAXA concept.

## 5. What a sweep shows

Both tables come from `abep_sweep` ([SWEEPS.md](SWEEPS.md)), for the default satellite (100 kg, 1 m² frontal area, C_D 2.2, an intake of 2 m², exhaust velocity 20 km/s, inclination 51.6 degrees, bus load 300 W) in the 1976 standard atmosphere. **They illustrate the model; they are not predictions for any satellite.**

The power rises steeply as the orbit goes down:

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

The hold power is the mean drag over the thrust density, so it goes as the inverse of the thrust density: that is the number to know. The array has to give more than the hold power because it also carries the 300 W bus load and the charge for the eclipse; above about 250 km it is those, and not the thruster, that size the array.

Whether it can hold at all is a matter of the intake, and does not depend on the altitude (Section 2). With the thrust density at 13.7 mN/kW:

| Capture efficiency | Intake limit over drag | Holds? |
|---:|---:|---|
| 0.10 | 0.48 | `IntakeLimited` at 160, 230 and 300 km: no power helps |
| 0.20 | 0.97 | `IntakeLimited` at all three, just short |
| 0.30 | 1.45 | `Holds` at all three |
| 0.46 | 2.22 | `Holds` at all three |

The altitude sets how much power is needed; the intake, the exhaust velocity and the frontal area set whether any power is enough. That second part of the question does not depend on the atmosphere.

## 6. Limits

The long form, with what has been checked for each, is [LIMITATIONS.md](LIMITATIONS.md).

- **Thrust is a parameter, not a prediction.** It is the power times an assumed thrust density, limited by an assumed capture. The default of 13.7 mN/kW lies in the 10 to 14 mN/kW range that Romano et al. quote for a JAXA concept; the primary paper is unread. The exhaust velocity has no sourced range. Ram drag is not modelled: charged against the thrust, it would make the net thrust lower by the fraction `v_rel / c_e` (37% lower at 20 km/s), and negative for any exhaust velocity below the orbital speed, such as the 3.9 km/s that Romano et al. estimate for their test device. The intake is two numbers, a capture efficiency and an area.
- **One fixed atmosphere.** The density at these altitudes varies by a large factor with solar and geomagnetic activity, and between day and night; the repository has no citable source for how large, and does not model it. Decay times and hold powers go with the density, so absolute numbers are indicative. A sweep cannot vary the atmosphere.
- **Simplified dynamics.** Gravity is a point mass plus J2: no higher harmonics, no Sun or Moon, no radiation pressure. The drag coefficient (2.2) and the frontal area are fixed, there are no winds and no attitude dynamics.
- **Power and flight modes.** A cylindrical shadow, a Sun fixed in space, an ideal battery and a constant bus load. The state machine's thresholds are policy, its thermal coefficients have no source, and it cannot see an eclipse coming.
- **Sources.** Most published numbers are second-hand. The primary papers of the JAXA, ESA and Pekker and Keidar studies are unread, and so is the primary source of the thrust-density range (Fujita, 2004). The WGS 84 constants were confirmed from secondary sources.
- **Platforms.** The Python module is tested on Linux only. The orbit view was tested by hand on one Windows PC and GPU, and draws a stylised Earth.

## 7. Reproduce it

C++17 and CMake 3.16 or newer; nothing else.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/abep_run altitude_km=300 mass_kg=150 duration_days=3 out=my_run
./build/abep_sweep altitude_km=160:300:8 thrust_density_mn_per_kw=10,12,14 out=hold_power.csv
```

The curves of Romano et al. that the tests use are in `data/romano2018_curves.csv`; `tools/verification/digitize_romano2018.py` reads them from the paper's PDF. A Windows download with the orbit view and `abep_run` is on [itch.io](https://overby-industries.itch.io/ion-propulsion-simulator).

## 8. What we would like criticised

1. **Ram drag and net thrust.** The model's thrust is gross, and the momentum of the incoming air stays inside `C_D * A`. Is that defensible? If it were charged against the thrust, what exhaust velocity should an ABEP thruster be assumed to have?
2. **The JAXA numbers.** Can anyone who has the primary study say which atmosphere, drag coefficient, reference area and definition of thrust to power it used for 470 W at 180 km and 3.3 kW at 140 km? Is the inlet-area reading of Section 4 anywhere near right?
3. **The atmosphere at low and high solar activity.** A citable table, or the output of a standard thermosphere model, for 140 to 300 km at low, mean and high solar activity, so that runs can bracket the answer.
4. **GOCE.** The drag coefficient and effective area that GOCE flew with, and the measured density at the time of ESA's 190 m/day, would turn case 25 from a factor of 3 into something sharper.
5. **The definition of the hold power.** An orbit average, with the thruster on all the time, against the drag over one orbit. Is that what published design points mean? If they size for a duty cycle or for the peak drag, case 28 compares unlike things.
6. **Intake and exhaust.** Any measured or defensible range for the capture efficiency and the exhaust velocity of an air-breathing thruster, and any ground-test or flight result that could be compared.
7. **Anything in [VALIDATION.md](VALIDATION.md) that looks wrong,** and any tolerance you would have chosen differently. If this note and VALIDATION.md disagree, VALIDATION.md is right, and we want to know.

Issues and pull requests are welcome on the [repository](https://github.com/Overby-Industries/ion-propulsion-simulator/issues).

## References

1. F. Romano, B. Massuti-Ballester, T. Binder, G. Herdrich, S. Fasoulas and T. Schönherr, "System analysis and test-bed for an atmosphere-breathing electric propulsion system using an inductive plasma thruster", *Acta Astronautica* 147 (2018) 114-126, [doi:10.1016/j.actaastro.2018.03.031](https://doi.org/10.1016/j.actaastro.2018.03.031). Cases 3, 3b, 3c, and the review of Section 1 for cases 26 to 28.
2. *U.S. Standard Atmosphere, 1976*, NOAA, NASA and the US Air Force, NOAA-S/T 76-1562 and NASA-TM-X-74335, [NTRS 19770009539](https://ntrs.nasa.gov/citations/19770009539), Part 4, Table I.
3. ESA, ["GOCE's electric ion propulsion engine switched on"](https://www.esa.int/Applications/Observing_the_Earth/FutureEO/GOCE/GOCE_s_electric_ion_propulsion_engine_switched_on), 6 April 2009, and ESA's GOCE operations pages (esa.int/Enabling_Support/Operations/GOCE). Cases 25 and 26.
4. A. Prša et al., "Nominal values for selected solar and planetary quantities: IAU 2015 Resolution B3", *Astronomical Journal* 152 (2016) 41, [doi:10.3847/0004-6256/152/2/41](https://doi.org/10.3847/0004-6256/152/2/41). The solar constant, 1361 W/m².
5. National Geospatial-Intelligence Agency, *Department of Defense World Geodetic System 1984*, NGA.STND.0036 (formerly NIMA TR8350.2). The four defining parameters; see [CONSTANTS.md](CONSTANTS.md) for how they were confirmed.
