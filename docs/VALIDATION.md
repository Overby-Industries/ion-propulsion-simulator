# Validation

What has been checked against what, and what has not. A model is only as trustworthy as this page. Cases are added before the code that needs them, and a case is only marked done when its test passes in CI.

| # | Case | Reference | Status |
|---|---|---|---|
| 1 | Circular-orbit period | Analytic: `T = 2 * pi * sqrt(a^3 / mu)` | Not started |
| 2 | J2 nodal precession of a circular orbit | Analytic, secular: `dOmega/dt = -3/2 * J2 * (Re / a)^2 * n * cos(i)` | Not started |
| 3 | Drag against altitude for a 1 m^2 satellite with Cd near 2 | A published drag-versus-altitude curve; source to be chosen and cited | Not started |
| 4 | Published results, at least three | ESA's GOCE mission and published ABEP studies; sources to be chosen and cited | Not started |

## Rules

- Every constant and every published number used in the code or in a test is cited here or in `CONSTANTS.md`, with where it came from.
- Tolerances are written down before the comparison is run, not adjusted to make it pass.
- If a case fails, the finding is recorded here whether or not it is later fixed.

## Known unknowns

- The thrust-density range assumed by the plan (10 to 13.7 mN/kW) is still to be traced to its published source.
- Real intake efficiency is unproven, so the capture-limited part of the thrust model has nothing to be validated against yet.
