# Atmosphere

The density the simulator flies through, where it comes from, how it was read, and what it cannot tell you. There are five atmospheres: the 1976 US Standard Atmosphere (`atmosphere=0`, the default, described first) and four profiles for low, moderate and high solar activity from ECSS-E-ST-10-04C (`atmosphere=1` to `4`, [further down](#the-solar-activity-profiles)).

## What is in the table

Air density of the **U.S. Standard Atmosphere, 1976**, from 100 km to 1000 km geometric altitude: 401 rows, to four significant figures, exactly as printed.

| Altitude range | Row spacing |
|---|---|
| 100 to 300 km | 1 km |
| 300 to 500 km | 2 km |
| 500 to 1000 km | 5 km |

In code: `abep::ussa1976::density_table()` in [`include/abep/standard_atmosphere_1976.hpp`](../include/abep/standard_atmosphere_1976.hpp) returns a [`DensityTable`](../include/abep/atmosphere.hpp), which interpolates log-linearly between rows (the density falls exponentially from one row to the next) and throws `std::out_of_range` outside 100 to 1000 km. A satellite that decays below 100 km stops the run; it is not given a made-up density.

## Where it comes from

*U.S. Standard Atmosphere, 1976*, prepared by NOAA, NASA and the US Air Force, October 1976. Report numbers NOAA-S/T 76-1562 and NASA-TM-X-74335; NASA's technical-reports record is [NTRS 19770009539](https://ntrs.nasa.gov/citations/19770009539), whose PDF is 243 pages and 17 MB. The numbers are from **Part 4, Table I** (geometric altitude, metric units), document pages 68 to 73, which are PDF pages 83 to 88.

The record of what the report prints is [`data/ussa1976_table1.txt`](../data/ussa1976_table1.txt), in the report's own notation: `5.604-7` is 5.604e-7, and an exponent is written only where it changes. Each row also carries the printed `rho/rho_0` column. `tools/ussa1976_table.py` validates that file and generates the C++ header from it; the header is never edited by hand.

`rho_0 = 1.2250 kg/m^3` is the standard's sea-level density (Table 10 of the report, document page 20), the divisor of the printed `rho/rho_0` column. It is used only to cross-check the transcription (see [CONSTANTS.md](CONSTANTS.md)); the table itself agrees with it in all 401 rows (below).

## How it was read, and how it was checked

The PDF is a scan; the text layer inside it is noise. The numbers were therefore **read by eye from rendered page images** (crops of the altitude column beside the two density columns, at 260 to 300 dpi) by the AI assistant (Claude) that wrote this code. No person has yet compared the file with the report row by row; a second pair of eyes is welcome, and takes a few minutes with the PDF open beside the data file, which uses the same layout as the printed table. In the meantime it was checked in three independent ways, and each result is reproducible:

1. **The report prints every density twice**, as `rho` and as `rho/rho_0`, each rounded separately to four figures. The two must agree to within the sum of their roundings. All 401 rows do (largest relative mismatch 6.8e-4, which is rounding). A misread digit or a slipped exponent breaks this. It is checked by `tools/ussa1976_table.py check` and, in an independent implementation, by `tests/test_standard_atmosphere_1976.cpp`, which runs in CI.
2. **An independent machine read.** [`tools/verification/read_scan_with_ocr.py`](../tools/verification/read_scan_with_ocr.py) runs an OCR engine (RapidOCR 1.4.4) over the same pages and compares. It read **797 of the 802 printed numbers identically** (399 of 401 densities and 398 of 401 ratios) and agreed on 94 of the 97 printed exponents, with no disagreement; it did not capture the other three. The five numbers it could not confirm were each looked at again on a 300 dpi crop of the report, and the data file matches the page in all five:

   | Altitude | Column | Data file | OCR |
   |---|---|---|---|
   | 180 km | rho | 5.194e-10 | not read |
   | 271 km | rho | 3.652e-11 | not read |
   | 237 km | rho/rho_0 | 6.946e-11 | garbled |
   | 520 km | rho/rho_0 | 3.099e-13 | 3.094 |
   | 630 km | rho/rho_0 | 6.096e-14 | 6.046 |

   The two misreads are also ruled out by check 1: 3.094 would put the 520 km density at 3.790e-13 against the printed 3.796e-13.
3. **Smoothness against another implementation.** The `ussa1976` Python package (0.3.4) is an independent implementation of the standard. It is *not* a substitute for the report (below), but the ratio of the printed density to the package's varies smoothly with altitude, so a misread digit would stand out as a bump larger than the table's own rounding. There is none in any of the 399 interior rows. [`tools/verification/compare_with_ussa1976_package.py`](../tools/verification/compare_with_ussa1976_package.py) runs this.

The tests also check the table's shape (401 rows, the report's spacing), that density falls at a physically sensible rate everywhere, and a dozen spot values typed separately from the data file. [VALIDATION.md](VALIDATION.md) has the tolerances and results, and the findings, including what went wrong.

**Why not use the package?** It is higher than the printed table by up to 6.7% (at 456 km), and by 6.5% at 400 km (2.984e-12 kg/m^3 against the printed 2.803e-12). The ratio printed / package:

| Altitude | 100 km | 150 km | 200 km | 300 km | 400 km | 500 km | 600 km | 800 km | 1000 km |
|---|---|---|---|---|---|---|---|---|---|
| Ratio | 0.9985 | 0.9843 | 0.9710 | 0.9492 | 0.9393 | 0.9374 | 0.9424 | 0.9734 | 0.9970 |

The printed table is the standard, so the printed table is what is used. The reason for the package's difference was not investigated.

## Interpolation

Between rows the density is interpolated log-linearly. There are no printed values between rows to compare with, so the error was measured by leaving every second row out and recovering it from its neighbours (a 2 km gap where the report prints 1 km, and so on). The worst case is **0.451% at 111 km**, against a tolerance of 0.5% set before the run.

That is for gaps twice as wide as the shipped table's. From the second differences of the printed rows themselves, the shipped table should be out by at most about **0.11% (near 111 km)** and by no more than about 0.02% anywhere above 150 km. These are estimates, not measurements, and the error does not fall quite as fast with the gap as a pure second-order scheme would suggest (see finding 7 in [VALIDATION.md](VALIDATION.md)). Above 150 km the error is comparable to the rounding of the printed figures themselves (0.005% to 0.05%), and all of it is far smaller than the differences between atmosphere models.

## What the standard does not describe

The report's introduction (Part 1) calls it an idealized, steady-state representation of the atmosphere, as it is assumed to exist in a period of moderate solar activity; its foreword gives the exospheric temperature as 1000 K and calls that representative of the mean for solar activity. That has consequences for a drag simulator:

- **One fixed profile.** No latitude, longitude, time of day, season or day-to-day variation, and above all **no solar-cycle or geomagnetic dependence**. Real density at orbital altitude moves by about an order of magnitude or more between solar minimum and maximum, so an ABEP design that just closes at the standard's density may not close at solar minimum, and a decay time computed from this table is a single point on a wide range, not a prediction. The four profiles below are the way to bracket it.
- **Altitude.** The table is indexed by geometric altitude above mean sea level. The simulator looks it up at the height above the WGS 84 ellipsoid (see finding 6 in [VALIDATION.md](VALIDATION.md)); the geoid is within roughly 100 m of the ellipsoid, which is about 0.2% in density at 400 km and is ignored.
- **Air co-rotates** with Earth in the drag calculation; the standard has nothing to say about winds.

**How far it is from another model.** Against the NRLMSISE-00 density at average solar activity (F10.7 = 140, Ap = 15) plotted by Romano et al. (2018), whose date and place are not stated, the standard's density is 1.34 times higher at 120 km, 1.20 at 150 km, 1.06 at 200 km and 1.11 at 250 km. That is the size of the difference between two reasonable atmospheres at the altitudes ABEP needs ([VALIDATION.md](VALIDATION.md), finding 11; [ABEP_MODEL.md](ABEP_MODEL.md)).

## The solar-activity profiles

Four more profiles, for low, moderate and high solar and geomagnetic activity, are chosen with the `atmosphere` setting (`atmosphere=0`, the default, is the 1976 standard above):

| `atmosphere` | Profile | F10.7 | Ap |
|---:|---|---:|---:|
| 1 | low | 65 | 0 |
| 2 | moderate | 140 | 15 |
| 3 | high, long term | 250 | 45 |
| 4 | high, short term | 300 (81-day average 250) | 240 |

**Where they come from.** Table G-4 of *ECSS-E-ST-10-04C, Space engineering: Space environment* (ESA Requirements and Standards Division, 15 November 2008, 198 pages, free from [ecss.nl](https://ecss.nl/wp-content/uploads/standards/ecss-e/ECSS-E-ST-10-04C15November2008.pdf)), Annex G, document pages 138 and 139: the total density of the JB-2006 model (Bowman, Tobiska, Marcos and Valladares, *J. Atmos. Sol.-Terr. Phys.* 70(5), 774-793, 2008, [doi:10.1016/j.jastp.2007.10.002](https://doi.org/10.1016/j.jastp.2007.10.002)) at equatorial latitude, averaged over diurnal and seasonal variations, from 100 to 900 km in 41 rows 20 km apart, to three significant figures. The record of what the standard prints is [`data/ecss_e_st_10_04c_table_g4.txt`](../data/ecss_e_st_10_04c_table_g4.txt), with a point for the standard's comma. `tools/ecss_table.py` validates it and generates [`include/abep/solar_activity_atmosphere.hpp`](../include/abep/solar_activity_atmosphere.hpp) from it, and the header is never edited by hand (`python tools/ecss_table.py check`, `generate`). Tables G-1 to G-3 of the same annex, the NRLMSISE-00 model (Picone, Hedin, Drob and Aikin, *J. Geophys. Res.* 107(A12), 1468, 2002, [doi:10.1029/2002JA009430](https://doi.org/10.1029/2002JA009430)) under the first three conditions, are in [`data/ecss_e_st_10_04c_tables_g1_g3.txt`](../data/ecss_e_st_10_04c_tables_g1_g3.txt) (temperature and density only). The simulation does not use them: they are a second, independent model, for the cross-check.

**How they were read and checked.** The PDF has a text layer (it is not a scan), read with `tools/verification/extract_ecss_tables.py`, which needs PyMuPDF and the PDF and is not run in the CI. Then:

1. **All 41 rows of Table G-4 were compared by eye with the rendered pages.**
2. `tools/ecss_table.py` and `tests/test_solar_activity.cpp` check the shape (41 rows, 100 to 900 km, 20 km apart), that each column falls with altitude and that from 120 km the four are ordered low < moderate < high long term < high short term, that the header equals the data file, and eight values typed apart from it.
3. **The other model.** JB-2006 over NRLMSISE-00, from the standard's own tables, is 0.78 to 1.18 from 120 to 600 km and varies smoothly, so a misprinted digit or exponent would stand out as a bump ([VALIDATION.md](VALIDATION.md), case 32).

What the standard leaves odd is in finding 29: one cell of Table G-3 prints `3,64E-4`; above 600 km the NRLMSISE-00 density in Tables G-1 to G-3 differs from the sum of its own number densities (1% to 13%), so it is used below 600 km only; and the four JB-2006 columns at 100 km are not ordered by activity.

**What the standard says about them.** JB-2006 is valid from 120 km up. The rows at 100 km are printed in Table G-4 and are kept, so that the tables reach the same lower edge as the 1976 standard and a run's decay still ends at 100 km; `decay_altitude_km=120` stays inside the model's validity. The tables end at 900 km, and a satellite above that stops the run. The estimated uncertainty of the JB-2006 total density is 10% to 15% at mean activity, depending on altitude, and more at the extremes: it "can have +/- 100% variation at 400-500 km for some activities and locations". The profiles are averages: no latitude, local time, season or date. The standard also says that high short-term activity is not to be combined with low or moderate long-term values.

**Interpolation.** Rows 20 km apart are too far apart for the log-linear rule of the 1976 table, whose rows are 1 to 5 km apart. Thinning the 1976 table to 20 km rows and recovering the others, log-linear interpolation is off by up to 18% below 150 km, 4.7% from 150 to 200 km and 1.0% above, and a monotone cubic of ln(density) by 8.4%, 0.38% and 0.07% ([VALIDATION.md](VALIDATION.md), case 31 and finding 26). The ECSS profiles therefore use the cubic (`DensityTable::Interpolation::MonotoneCubic`); the 1976 table keeps log-linear, so every result with it is what it was.

**How far apart they are.** JB-2006 divided by the 1976 standard, and the high long-term profile over the low one:

| Altitude | low | moderate | high, long term | high, short term | high over low |
|---:|---:|---:|---:|---:|---:|
| 140 km | 0.81 | 1.04 | 1.13 | 1.16 | 1.4 |
| 200 km | 0.58 | 1.25 | 1.61 | 1.72 | 2.8 |
| 300 km | 0.31 | 1.40 | 2.61 | 3.25 | 8.5 |
| 400 km | 0.17 | 1.51 | 3.89 | 5.60 | 23.5 |

Near 140 km solar activity hardly matters and at 400 km it is everything, which is why the hold power of a satellite and its time to decay span a wide bracket with the altitude ([SWEEPS.md](SWEEPS.md)). The moderate profile is **not** closer to the NRLMSISE-00 curve of Romano et al. (2018) than the 1976 standard is: 1.27 to 1.51 times it from 120 to 250 km, against 1.06 to 1.34 for the standard (finding 27). The name says the level of activity, not a precise atmosphere.

**Copyright.** The standard is copyright 2008 by the European Space Agency for the members of ECSS, and the text of it that was read grants no licence for reuse. The numbers of Table G-4 are reproduced with attribution (the data file and the header say whose they are). Anyone who relies on this repository's licence for them should ask the ECSS secretariat.

## Using a different atmosphere

`DensityTable` accepts any strictly increasing altitude and positive density table, with log-linear or monotone-cubic interpolation, so another atmosphere can be added as soon as one with a citable source is: a table in `data/`, a generated header like `solar_activity_atmosphere.hpp`, and a case in [VALIDATION.md](VALIDATION.md) before it is used. `simulate(settings, table)` flies any table; the `atmosphere` setting chooses among the five built in. The most useful contribution here is a citable source for how density varies with latitude, local time and season, or a series of actual F10.7 and Ap values by date (see [CONTRIBUTING.md](../CONTRIBUTING.md)).

## Reproducing and changing the table

```powershell
python tools/ussa1976_table.py check       # validate the data file; confirm the header matches it
python tools/ussa1976_table.py generate    # rewrite the header from the data file
```

The Python tools use only the standard library. The two scripts in `tools/verification/` need extra packages, named in their docstrings, and are not run in CI.
