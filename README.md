# ABEP: Ion Thrusters Simulator

**An open, deterministic simulator for one question: how much power must an air-breathing electric thruster have to hold a satellite's altitude in low Earth orbit?**

**Status: early.** The orbit propagator (two-body gravity plus J2), drag and the density-table interpolation are written and tested against known answers. No atmosphere data is included yet, because the only implementation found disagrees with the published standard and the printed table has not been read; the ABEP thrust model, the power model and the state machine are not started. See [`docs/SPEC.md`](docs/SPEC.md) for what v0.1 will do and [`docs/VALIDATION.md`](docs/VALIDATION.md) for what has and has not been checked, including the failed first attempts.

## What it will do

Given a satellite's mass, frontal area and drag coefficient, an orbit, a solar-power budget and an assumed thrust density, the simulator will propagate the orbit (two-body gravity plus J2) through a standard atmosphere, apply drag and thrust, and report net drag, altitude over time and thruster duty cycle, including eclipses. Results come out as CSV. Python bindings for plotting, and an optional orbit view, come later.

## Limits

- **It does not predict thruster performance.** Thrust here is a parameter: power times an assumed thrust density, limited by an assumed intake capture. Real intake efficiency is unproven, and, to our knowledge, ground tests have not yet shown net positive thrust. Read the outputs as sensitivities ("if the thrust density were X, the duty cycle would be Y"), never as evidence that a thruster will work.
- **Static atmosphere.** Real density varies strongly with solar and geomagnetic activity, so absolute decay times are only indicative.
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
2. Core physics: a two-body plus J2 orbit propagator, a standard-atmosphere density model and drag, tested against known answers *(propagator, J2 and drag done; the atmosphere table is waiting on a checked source)*
3. ABEP model: thrust from power and thrust density, limited by intake capture, with solar power and eclipse
4. A deterministic flight state machine: SAFE, CHARGE, COAST, THRUST, FAULT
5. CSV output and Python bindings; an optional orbit view that must not delay the core
6. Validation against at least three published results, a limitations write-up, parameter sweeps
7. The v0.1 release, with a short technical note and a walkthrough video
8. A retrospective, and a choice of the next module

## Part of Overby Industries

This repository is one of the software-first projects described in the [Overby Industries profile](https://github.com/Overby-Industries). The propulsion concept it examines is described in [`power-and-propulsion`](https://github.com/Overby-Industries/power-and-propulsion) and in the papers in [`whitepapers`](https://github.com/Overby-Industries/whitepapers).

## Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md) and [`CODE_OF_CONDUCT.md`](CODE_OF_CONDUCT.md). The most useful contributions right now are sources: a published thrust-density figure, a published drag-versus-altitude curve, or a critique of the model assumptions in the spec.

## License

[Apache 2.0](LICENSE).
