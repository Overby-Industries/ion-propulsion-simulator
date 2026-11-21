#!/usr/bin/env python3
"""Compare data/ussa1976_table1.txt with the `ussa1976` Python package, an independent implementation of the 1976 standard.

    pip install ussa1976 numpy
    python tools/verification/compare_with_ussa1976_package.py

Two things come out of this. First, how far the package is from the printed table (it is not a substitute for it:
see docs/ATMOSPHERE.md). Second, a check on the transcription: the ratio of the printed density to the package's
varies smoothly with altitude, so a misread digit in the data file would show up as a bump in that ratio, larger
than the table's own rounding. Not run in CI. The exit status is 1 if any bump is found.
"""
import importlib.util
import math
import sys
from importlib import metadata
from pathlib import Path

import numpy as np
import ussa1976

TOOLS = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("ussa1976_table", TOOLS / "ussa1976_table.py")
table = importlib.util.module_from_spec(spec)
spec.loader.exec_module(table)

rows = table.read_rows(TOOLS.parent / "data" / "ussa1976_table1.txt")
z = np.array([r.z_m for r in rows], dtype=float)
printed = np.array([r.rho for r in rows])
half_width = np.array([r.rho_half_width / r.rho for r in rows])   # relative rounding of each printed density
package = np.asarray(ussa1976.compute(z=z, variables=["rho"])["rho"].values, dtype=float)
ratio = printed / package

print("ussa1976 %s against the printed table (printed / package):" % metadata.version("ussa1976"))
for km in (100, 120, 150, 200, 250, 300, 400, 500, 600, 800, 1000):
    i = int(np.argmin(abs(z - km * 1000.0)))
    print("  %5d km: %.4f" % (km, ratio[i]))
worst = int(np.argmax(abs(1.0 / ratio - 1.0)))
print("largest difference: the package is %+.2f%% from the printed density, at %.0f km"
      % (100.0 * (1.0 / ratio[worst] - 1.0), z[worst] / 1000.0))

# Smoothness: compare ln(ratio) at each interior row with the straight line through its neighbours. The allowance is
# the rounding of the three printed densities involved plus 3e-4 for the slow curvature of the true ratio.
log_ratio = np.log(ratio)
bumps = []
for i in range(1, len(z) - 1):
    t = (z[i] - z[i - 1]) / (z[i + 1] - z[i - 1])
    residual = log_ratio[i] - ((1 - t) * log_ratio[i - 1] + t * log_ratio[i + 1])
    allowed = half_width[i] + (1 - t) * half_width[i - 1] + t * half_width[i + 1] + 3.0e-4
    if abs(residual) > allowed:
        bumps.append((z[i] / 1000.0, residual, allowed))
print("rows where the ratio is not smooth beyond rounding: %d of %d" % (len(bumps), len(z) - 2))
for km, residual, allowed in bumps:
    print("  %.0f km: bump %+.2e, allowed %.2e" % (km, residual, allowed))
sys.exit(1 if bumps else 0)
