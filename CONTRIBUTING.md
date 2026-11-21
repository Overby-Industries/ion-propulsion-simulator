# Contributing to ABEP: Ion Thrusters Simulator

First off, thank you for your interest in contributing — every bit of help, whether code, knowledge, or review, moves us closer to building a sustainable, ethical space economy.

## Code of Conduct

We follow a **positive, respectful, and collaborative** mindset. No harassment, ego-driven behavior, or toxic interactions. This project is for everyone. See [`CODE_OF_CONDUCT.md`](CODE_OF_CONDUCT.md).

---

## How to Contribute

### 1. Fork and clone

Fork the repository, clone it locally, and create a feature branch:

```bash
git checkout -b feature/my-improvement
```

### 2. Build and test

C++17 and CMake 3.16 or newer. There are no other dependencies.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

### 3. Open a pull request

- Keep each pull request focused on one change, and say what it changes and why.
- CI must pass on Linux and Windows.
- Update the docs when behavior changes.

---

## What this project needs

Sources and critique matter as much as code.

- **Cite everything.** Every constant, model and published number must be traceable to its source, cited in [`docs/VALIDATION.md`](docs/VALIDATION.md) (or `docs/CONSTANTS.md` once it exists).
- **State tolerances up front.** A physics test says what it compares against and how close counts as agreement, before it is run, not after.
- **Be honest about limits.** If a result depends on an assumption that is not established (real intake efficiency is the main one), say so where the result is shown.
- **Stay deterministic.** No random numbers, no adaptive time steps, no fast-math: the same inputs must give the same outputs.
- **Disagree with the model.** Critiques of the assumptions in [`docs/SPEC.md`](docs/SPEC.md) are welcome as issues; the *Knowledge Contribution* template is for this.
