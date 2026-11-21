# Constants

Every constant the simulator uses, where it comes from, and what has and has not been checked. SI units throughout.

| Constant | Symbol | Value | Basis |
|---|---|---|---|
| Earth equatorial radius | a | 6378137.0 m | WGS 84 defining parameter |
| Earth inverse flattening | 1/f | 298.257223563 | WGS 84 defining parameter |
| Earth gravitational parameter, atmosphere included | GM | 3.986004418e14 m^3/s^2 | WGS 84 defining parameter |
| Earth nominal mean angular velocity | omega | 7.292115e-5 rad/s | WGS 84 defining parameter |
| Second-degree zonal harmonic | J2 | 1.0826298213133063e-3 | Derived from the four above |

## Where the values came from

The four defining parameters are the WGS 84 set published by the US National Geospatial-Intelligence Agency (NGA.STND.0036, formerly NIMA TR8350.2). The NGA pages could not be opened from the environment this was written in (the site did not respond, and a UN copy of the document returned 404), so the values were confirmed from two secondary sources: a web search result quoting the NGA table, and the documentation page for the AHRS library, <https://ahrs.readthedocs.io/en/latest/geodesy/wgs84.html>, which prints all four. They agree with each other.

**To do before v0.1:** check the four values against the primary NGA document.

The NGA notes that GPS users use a slightly different GM (3.9860050e14 m^3/s^2). This project uses the WGS 84 value.

## J2

J2 is not a WGS 84 defining parameter; it is derived. For the level ellipsoid defined by the four parameters, the second-degree zonal harmonic of its normal gravity field is

```
J2 = (e^2 / 3) * (1 - (2/15) * m * e' / q0)
```

with `e^2 = 2f - f^2`, `m = omega^2 a^2 b / GM`, `e' = sqrt(a^2 - b^2) / b` and `q0 = 1/2 * ((1 + 3/e'^2) * atan(e') - 3/e')`. Evaluated to 60 digits this is **1.08262982131330627...e-3**; the constant above is that value rounded to a double.

Cross-check against a published number: the AHRS page prints the ellipsoid's normalized geometric second-degree zonal harmonic, `C2,0 = -4.84166774985e-4`. Since `J2 = -sqrt(5) * C2,0`, that gives 1.08262982131330472e-3, which agrees with the derived value to 1.4e-16 relative (the page gives 12 digits).

`tests/test_constants.cpp` recomputes J2 from the defining parameters, using a series for `q0` that avoids the cancellation in the formula as written, and fails if the stored value drifts. See [VALIDATION.md](VALIDATION.md) for the first attempt, which did not use the series and failed.

## Not used

EGM2008's empirical C2,0 (`-4.84165143790815e-4`, also on the AHRS page) describes the real Earth's field rather than the ellipsoid, and differs from the ellipsoid-derived value by 3.4e-6 relative. The ellipsoid-derived J2 is used here because it is consistent with the other four constants by construction.

## Still to pin

The solar constant, needed for the power model, is added with its source when that model is written.
