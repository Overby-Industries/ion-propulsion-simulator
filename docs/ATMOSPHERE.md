# Atmosphere

The density the simulator flies through, where it comes from, how it was read, and what it cannot tell you.

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

- **One fixed profile.** No latitude, longitude, time of day, season or day-to-day variation, and above all **no solar-cycle or geomagnetic dependence**. Real density at orbital altitude moves by about an order of magnitude or more between solar minimum and maximum, so an ABEP design that just closes at the standard's density may not close at solar minimum, and a decay time computed from this table is a single point on a wide range, not a prediction.
- **Altitude.** The table is indexed by geometric altitude above mean sea level. The simulator looks it up at the height above the WGS 84 ellipsoid (see finding 6 in [VALIDATION.md](VALIDATION.md)); the geoid is within roughly 100 m of the ellipsoid, which is about 0.2% in density at 400 km and is ignored.
- **Air co-rotates** with Earth in the drag calculation; the standard has nothing to say about winds.

## Using a different atmosphere

`DensityTable` accepts any strictly increasing altitude and positive density table, so a low- or high-solar-activity profile can be swapped in as soon as one with a citable source is added. That is the natural next step for a sensitivity tool, and the most useful contribution here: a published density-versus-altitude table for solar minimum or maximum (see [CONTRIBUTING.md](../CONTRIBUTING.md)).

## Reproducing and changing the table

```powershell
python tools/ussa1976_table.py check       # validate the data file; confirm the header matches it
python tools/ussa1976_table.py generate    # rewrite the header from the data file
```

The Python tools use only the standard library. The two scripts in `tools/verification/` need extra packages, named in their docstrings, and are not run in CI.
