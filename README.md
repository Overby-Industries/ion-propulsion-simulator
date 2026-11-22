# ABEP: Ion Thrusters Simulator

**An open, deterministic simulator for one question: how much power must an air-breathing electric thruster have to hold a satellite's altitude in low Earth orbit?**

**Status: early.** The orbit propagator (two-body gravity plus J2), drag, and the density of the 1976 US Standard Atmosphere from 100 to 1000 km are written and tested against known answers. The atmosphere is the printed table, read from the report and checked several ways ([`docs/ATMOSPHERE.md`](docs/ATMOSPHERE.md)). The ABEP thrust model (limited by power or by the intake), the eclipse and solar-power model and an ideal battery are written too, and the drag and intake mass flow agree with a published paper's curves to within 1% ([`docs/ABEP_MODEL.md`](docs/ABEP_MODEL.md)). The flight state machine (SAFE, CHARGE, COAST, THRUST, FAULT) is written and tested too, with a thruster temperature and a mission run that drives it through the orbit, power and thermal models ([`docs/FLIGHT_MODES.md`](docs/FLIGHT_MODES.md)); its thresholds are operating policy and its thermal numbers are assumptions. The CSV output is not started. See [`docs/SPEC.md`](docs/SPEC.md) for what v0.1 will do and [`docs/VALIDATION.md`](docs/VALIDATION.md) for what has and has not been checked, including the failed first attempts.

## What it will do

Given a satellite's mass, frontal area and drag coefficient, an orbit, a solar-power budget and an assumed thrust density, the simulator will propagate the orbit (two-body gravity plus J2) through a standard atmosphere, apply drag and thrust, and report net drag, altitude over time and thruster duty cycle, including eclipses. Results come out as CSV. Python bindings for plotting, and an optional orbit view, come later.

## Limits

- **It does not predict thruster performance.** Thrust here is a parameter: power times an assumed thrust density, limited by an assumed intake capture. Real intake efficiency is unproven, and, to our knowledge, ground tests have not yet shown net positive thrust. Read the outputs as sensitivities ("if the thrust density were X, the duty cycle would be Y"), never as evidence that a thruster will work.
- **One fixed atmosphere.** The density table is the 1976 US Standard Atmosphere, a single profile for moderate solar activity. Real density varies strongly with solar and geomagnetic activity, so absolute decay times are only indicative.
- **Simplified dynamics.** Gravity is a point mass plus J2 (no higher harmonics), with a fixed frontal area, no attitude dynamics, no third bodies, no solar radiation pressure.
- **Early.** Until [`docs/VALIDATION.md`](docs/VALIDATION.md) says a case is done, treat that part of the model as unchecked.

## Build and test

C++17 and CMake 3.16 or newer. No other dependency.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## Roadmap to v0.1

1. Scope, repository and specification *(done)*
2. Core physics: a two-body plus J2 orbit propagator, a standard-atmosphere density model and drag, tested against known answers *(done: propagator, J2, drag and the 1976 standard-atmosphere table)*
3. ABEP model: thrust from power and thrust density, limited by intake capture, with solar power and eclipse *(done, and checked against a published drag curve; the exhaust velocity and the ram-drag question are open, see [`docs/ABEP_MODEL.md`](docs/ABEP_MODEL.md))*
4. A deterministic flight state machine: SAFE, CHARGE, COAST, THRUST, FAULT *(done, with a mission run that couples it to the orbit, power and thermal models, see [`docs/FLIGHT_MODES.md`](docs/FLIGHT_MODES.md))*
5. CSV output and Python bindings; an optional orbit view that must not delay the core
6. Validation against at least three published results, a limitations write-up, parameter sweeps
7. The v0.1 release, with a short technical note and a walkthrough video
8. A retrospective, and a choice of the next module

## Part of Overby Industries

This repository is one of the software-first projects described in the [Overby Industries profile](https://github.com/Overby-Industries). The propulsion concept it examines is described in [`power-and-propulsion`](https://github.com/Overby-Industries/power-and-propulsion) and in the papers in [`whitepapers`](https://github.com/Overby-Industries/whitepapers).

## Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md) and [`CODE_OF_CONDUCT.md`](CODE_OF_CONDUCT.md). The most useful contributions right now are sources: a published thrust-density figure, a published drag-versus-altitude curve, a citable density table for low or high solar activity, a second reading of the atmosphere table against the report, or a critique of the model assumptions in the spec.

## License

[Apache 2.0](LICENSE).
