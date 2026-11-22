# The ABEP model

What the simulator computes about an air-breathing electric thruster, with the equations, where each number comes from, and what is left out. **This is a parameterised model, not a thruster model.** Thrust density, exhaust velocity and capture efficiency are inputs; nothing here says that any real thruster or intake achieves them. Real intake efficiency is unproven, and, to our knowledge, ground tests have not yet shown net positive thrust. Read the results as sensitivities.

## The equations

Code: [`drag.hpp`](../include/abep/drag.hpp), [`thrust.hpp`](../include/abep/thrust.hpp), [`eclipse.hpp`](../include/abep/eclipse.hpp), [`power.hpp`](../include/abep/power.hpp). Symbols: `rho` air density, `v_rel` speed relative to the air, `C_D` drag coefficient, `A` frontal area, `A_in` intake area, `eta_c` capture efficiency, `tau` thrust density, `c_e` exhaust velocity, `P` electrical power.

**Drag.** `D = 1/2 * rho * v_rel^2 * C_D * A`, with the air co-rotating with Earth. This is Eq. 1 of Romano et al. (2018, below).

**Collected air.** `mdot_c = eta_c * rho * v_rel * A_in`: the mass flow the intake delivers to the thruster (Romano et al., Eq. 9).

**Thrust.** The thruster would produce `tau * P` if it had all the air it wanted, and at most `mdot_c * c_e` with the air it can collect:

```
F = min( tau * P , mdot_c * c_e )
```

The electrical power actually drawn is `F / tau` (less than `P` when air is the limit) and the air actually used is `F / c_e`. Which limit applies is reported with the result, and a tie counts as power-limited. This form, thrust as collected mass flow times exhaust velocity, is Romano et al.'s Eq. 9, which they use as an upper bound; the cap by `tau * P` is this project's addition, so that the plan's input (power times thrust density) still sets the thrust when there is air to spare.

**Energy has to balance.** The jet carries `1/2 * F * c_e` of power out of `F / tau` put in, an efficiency of `1/2 * tau * c_e`. That cannot exceed 1, so a thrust density and an exhaust velocity with `tau * c_e > 2` (a dimensionless product) are refused as inconsistent; for `tau` = 13.7 mN/kW that means `c_e` above 146 km/s. The implied efficiency is available as `implied_thruster_efficiency`: at `tau` = 13.7 mN/kW and `c_e` = 20 km/s it is 13.7%, so the model is not claiming a good thruster by accepting those numbers.

**Whether the intake can keep up does not depend on altitude.** Both the collected momentum and the drag are proportional to `rho`, which cancels:

```
(mdot_c * c_e) / D = 2 * eta_c * (A_in / A) * c_e / (C_D * v_rel)
```

With `A_in = A`, `C_D` = 2.2 and `v_rel` = 7.3 km/s, the intake can supply a thrust equal to the drag only if `eta_c * c_e` is at least 8.0 km/s. What changes with altitude is the *power*: to hold altitude when air is not the limit, `P = D / tau`. For a satellite of 1 m² with `C_D` = 2.2 in a circular equatorial orbit in the 1976 standard atmosphere (this model, co-rotating air):

| Altitude | Drag | Power to offset it, `tau` = 13.7 mN/kW | `tau` = 10 mN/kW |
|---|---|---|---|
| 150 km | 123 mN | 9.0 kW | 12.3 kW |
| 170 km | 46 mN | 3.4 kW | 4.6 kW |
| 200 km | 14.9 mN | 1.09 kW | 1.49 kW |
| 250 km | 3.5 mN | 0.26 kW | 0.35 kW |

These are the model's arithmetic for illustration, not predictions. Real density moves them by an order of magnitude with solar activity ([ATMOSPHERE.md](ATMOSPHERE.md)); the `atmosphere` setting runs them in the low, moderate and high profiles ([SWEEPS.md](SWEEPS.md)).

**Shadow.** Earth's shadow is a cylinder of Earth's equatorial radius `R` along the anti-Sun axis. A point `r` is in shadow if `r . s < 0` and `|r|^2 - (r . s)^2 < R^2`, where `s` is the unit vector towards the Sun. For a circular orbit of radius `r` whose plane makes the angle `beta` with `s`, a point at angle `theta` from the projection of `s` on the orbit has `r_hat . s = cos(theta) * cos(beta)`. It is in shadow when `cos(theta) < 0` and `cos(theta)^2 * cos(beta)^2 > 1 - (R / r)^2`, that is for `theta` within `phi` of 180 degrees, where `cos(phi) = sqrt(1 - (R / r)^2) / cos(beta)`. The shadow covers an arc of `2 * phi`, so the fraction of the orbit in shadow is `2 * phi / (2 * pi) = phi / pi`:

```
f = acos( sqrt(1 - (R / r)^2) / cos(beta) ) / pi      for |beta| < asin(R / r), else 0
```

For `beta` = 0 this is `asin(R / r) / pi`, 0.391 for a 400 km orbit. The Sun is a fixed direction (an input), not an ephemeris; see the limits below.

**Power.** A solar array gives `S * A_array * eta * cos(incidence)`, with `S` = 1361 W/m² (the IAU 2015 nominal value, [CONSTANTS.md](CONSTANTS.md)); `eta` is the array efficiency you give it. The battery is an ideal store with a capacity and a floor of zero. Averaged over orbits, with a battery big enough to ride through the eclipse, the thruster can run for the fraction

```
duty = clamp( (P_array * (1 - f) - P_load) / P_thruster , 0 , 1 )
```

of the time. The deterministic flight state machine that decides when to thrust, charge and coast uses these pieces: see [FLIGHT_MODES.md](FLIGHT_MODES.md). Run against a propagated orbit it reaches this duty to within the battery's capacity ([VALIDATION.md](VALIDATION.md), case 14).

## Where the numbers come from

| Quantity | Value or range | Source | Status |
|---|---|---|---|
| Thrust density `tau` | 10 to 14 mN/kW (the plan: 10 to 13.7) | JAXA's air-breathing ion engine concept (170 km, frontal area 1.5 m², inlet 0.48 m², `eta_c` up to 46%), as reported in the literature review of Romano et al. (2018). Their Table 1 spans 10 to 59 mN/kW across the studies they list. | The 10 to 14 range and the table are read from the paper. The exact "13.7" wording is said to be in the IAC-17 version of that paper, which could not be opened, and the primary JAXA document that paper cites (K. Fujita, "Air-intake performance estimation of air-breathing ion engines", *Trans. Jpn. Soc. Mech. Eng. B* 70(700), 2004, 3038-3044) has not been read. |
| Capture efficiency `eta_c` | 0.35 (Busek, Diamant), 0.46 (JAXA), 0.90 (Shabshelowitz) | Romano et al. (2018), section 2.3, citing those studies | Read from the paper. These are design assumptions in those studies, not measurements. |
| Drag coefficient `C_D` | 2.2 | Romano et al. (2018), section 2.4: "a typical average in literature for small S/C in LEO" | Read from the paper. |
| Exhaust velocity `c_e` | no range assumed | The paper estimates at most 3.9 km/s for the IPG6-S test device (its Eq. 8, from a measured plasma enthalpy of 7.5 MJ/kg). No range is adopted here: it is an input, and the model refuses values that violate energy conservation. | An open input, the least constrained one. |
| Solar array efficiency | 0.295 used in the tests | Romano et al. (2018), section 2.5: a minimum average beginning-of-life efficiency of 29.5% | Illustrative; the model takes whatever you give it. |
| Solar irradiance `S` | 1361 W/m² | IAU 2015 Resolution B3, nominal total solar irradiance | A convention, not a measurement. |

Romano, F., Massuti-Ballester, B., Binder, T., Herdrich, G., Fasoulas, S. and Schönherr, T., "System analysis and test-bed for an atmosphere-breathing electric propulsion system using an inductive plasma thruster", *Acta Astronautica* 147 (2018) 114-126, [doi:10.1016/j.actaastro.2018.03.031](https://doi.org/10.1016/j.actaastro.2018.03.031). An open copy was at <https://discoverer.space/wp-content/uploads/2021/07/1-s2.0-S009457651730406X-main.pdf> when this was written.

## How the published curves are used

Three plots in that paper are vector drawings, so their curves can be read from the PDF's own coordinates instead of by eye: the density in an average-solar-activity NRLMSISE-00 atmosphere (Fig. 3), the drag on a 1 m² satellite with `C_D` = 2.2 in it (Fig. 6), and the collectable mass flow for three capture efficiencies (Fig. 5). [`tools/verification/digitize_romano2018.py`](../tools/verification/digitize_romano2018.py) reads them into [`data/romano2018_curves.csv`](../data/romano2018_curves.csv), 14 altitudes from 120 to 250 km, and `tests/test_romano2018.cpp` checks the simulator against them. The results and a first attempt that failed are in [VALIDATION.md](VALIDATION.md), cases 3, 3b and 3c and finding 10.

## What is not modelled

- **Ram drag of the collected air.** Air that enters the intake arrives at `v_rel` and carries momentum `mdot * v_rel` that the satellite must absorb. Here that is inside `C_D * A` for the whole body, and the thrust is a gross thrust. Charging it against the thrust instead would give a net thrust of `mdot * (c_e - v_rel)`, which is lower by the fraction `v_rel / c_e`: 37% lower at 20 km/s, and negative (a net drag) for any exhaust velocity below `v_rel`, such as the 3.9 km/s the paper estimates for its test device. Whether this belongs in `C_D * A` is a question for the reader to judge; it is the first thing to examine.
- **Intake physics.** Transmission probability, compression, backflow, and how `eta_c` changes with altitude, speed or gas composition. `eta_c` is a single number.
- **Thruster physics.** Ionisation, plasma behaviour, minimum density to run, erosion by atomic oxygen. `tau` and `c_e` are constants, independent of density and power. The only heating is the lumped thermal node of [FLIGHT_MODES.md](FLIGHT_MODES.md), whose coefficients are assumptions, and the only density limit is a threshold the user sets.
- **The atmosphere.** One fixed profile: the 1976 standard, or one of four for low, moderate and high solar activity (`atmosphere=1` to `4`); no latitude, time-of-day or date variation, and no composition ([ATMOSPHERE.md](ATMOSPHERE.md)).
- **Attitude.** The array always faces the Sun when lit, the drag area and `C_D` are constant, and the intake always faces the flow.
- **Shadow and Sun.** No penumbra, no atmospheric refraction, no oblateness in the shadow cylinder, and the Sun does not move over the run (it moves about 1 degree a day, so runs of weeks need the Sun direction updated).
- **Power.** The battery is ideal (no losses, rate limit or ageing) and there is no array degradation or temperature effect.
