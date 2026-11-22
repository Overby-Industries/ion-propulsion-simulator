#!/usr/bin/env python3
"""Validate the ECSS-E-ST-10-04C density tables and generate the C++ header from Table G-4.

    python tools/ecss_table.py check      validate the two data files, and that the header matches Table G-4
    python tools/ecss_table.py generate   rewrite include/abep/solar_activity_atmosphere.hpp from data/ecss_e_st_10_04c_table_g4.txt

Standard library only. The exit status is non-zero if a check fails. The data files are the record of what the standard prints
(docs/ATMOSPHERE.md says where, and how they were read and checked); the header is derived from Table G-4 and is never edited by
hand. Tables G-1 to G-3 (NRLMSISE-00) are not used by the simulation: they are a second, independent model for the cross-check
here and in tests/test_solar_activity.cpp.
"""
import math
import re
import sys
from pathlib import Path
from typing import List, NamedTuple

ROOT = Path(__file__).resolve().parent.parent
G4 = ROOT / "data" / "ecss_e_st_10_04c_table_g4.txt"
G123 = ROOT / "data" / "ecss_e_st_10_04c_tables_g1_g3.txt"
HEADER = ROOT / "include" / "abep" / "solar_activity_atmosphere.hpp"

COLUMNS = ("low", "moderate", "high, long term", "high, short term")
NUMBER = re.compile(r"^\d\.\d\dE[+-]\d\d$")  # three significant figures, as the standard prints them (with a point for its comma)


class Row4(NamedTuple):
    h_km: int
    text: List[str]  # the four densities as printed

    @property
    def rho(self) -> List[float]:
        return [float(t) for t in self.text]


class Row123(NamedTuple):
    h_km: int
    temperature_k: List[float]  # low, moderate, high
    rho: List[float]


def data_lines(path: Path):
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if line and not line.startswith("#"):
            yield number, line.split()


def read_g4(path: Path) -> List[Row4]:
    rows = []
    for number, fields in data_lines(path):
        if len(fields) != 5:
            raise ValueError("%s:%d: expected 5 fields, got %d" % (path.name, number, len(fields)))
        for token in fields[1:]:
            if not NUMBER.match(token):
                raise ValueError("%s:%d: cannot read %r" % (path.name, number, token))
        rows.append(Row4(int(fields[0]), fields[1:]))
    return rows


def read_g123(path: Path) -> List[Row123]:
    rows = []
    for number, fields in data_lines(path):
        if len(fields) != 7:
            raise ValueError("%s:%d: expected 7 fields, got %d" % (path.name, number, len(fields)))
        for token in fields[2::2]:
            if not NUMBER.match(token):
                raise ValueError("%s:%d: cannot read %r" % (path.name, number, token))
        rows.append(Row123(int(fields[0]), [float(x) for x in fields[1::2]], [float(x) for x in fields[2::2]]))
    return rows


def problems(g4: List[Row4], g123: List[Row123]) -> List[str]:
    found = []
    if [r.h_km for r in g4] != list(range(100, 901, 20)):
        found.append("Table G-4 must have 41 rows, from 100 km to 900 km every 20 km")
    if [r.h_km for r in g123] != list(range(0, 901, 20)):
        found.append("Tables G-1 to G-3 must have 46 rows, from 0 km to 900 km every 20 km")
    if found:
        return found
    for column, name in enumerate(COLUMNS):
        for a, b in zip(g4, g4[1:]):
            if not b.rho[column] < a.rho[column]:
                found.append("G-4 %s: density does not fall between %d km and %d km" % (name, a.h_km, b.h_km))
                continue
            # A density scale height between about 4 km and 250 km: a slip of a factor of ten in an exponent would put
            # the slope far outside this.
            slope = -math.log(b.rho[column] / a.rho[column]) / (b.h_km - a.h_km)
            if not 0.002 <= slope <= 0.25:
                found.append("G-4 %s: log-density slope %.4f per km between %d km and %d km is implausible" % (name, slope, a.h_km, b.h_km))
    for r in g4:
        # The standard's model is valid from 120 km; the 100 km row is printed in the table, and is not ordered by activity.
        if r.h_km >= 120 and not (r.rho[0] < r.rho[1] < r.rho[2] < r.rho[3]):
            found.append("G-4 at %d km: the four columns are not ordered low < moderate < high, long term < high, short term" % r.h_km)
    for a, b in zip(g123, g123[1:]):
        for case in range(3):
            if not b.rho[case] < a.rho[case]:
                found.append("G-%d: density does not fall between %d km and %d km" % (case + 1, a.h_km, b.h_km))
    for r in g123:
        for case in range(3):
            if not 150.0 <= r.temperature_k[case] <= 2000.0:
                found.append("G-%d at %d km: temperature %.0f K is implausible" % (case + 1, r.h_km, r.temperature_k[case]))
    # The two models, JB-2006 (G-4) and NRLMSISE-00 (G-1 to G-3), against each other from 120 to 600 km (above that the
    # standard's own NRLMSISE-00 density differs from the sum of its number densities, by up to 13%). They are different
    # models, so the ratio is only asked to lie between 0.5 and 2 and to vary smoothly: a misprinted digit or exponent
    # shows up as a bump. The check catches a transcription slip. It does not validate either model.
    by_h = {r.h_km: r for r in g123}
    for case in range(3):
        series = [(r.h_km, math.log(r.rho[case] / by_h[r.h_km].rho[case])) for r in g4 if 120 <= r.h_km <= 600]
        for h, log_ratio in series:
            if not 0.5 <= math.exp(log_ratio) <= 2.0:
                found.append("JB-2006 over NRLMSISE-00 (%s) at %d km is %.2f, outside 0.5 to 2" % (COLUMNS[case], h, math.exp(log_ratio)))
        for i in range(1, len(series) - 1):
            second = abs(series[i - 1][1] - 2.0 * series[i][1] + series[i + 1][1])
            if second > 0.3:
                found.append("JB-2006 over NRLMSISE-00 (%s) is not smooth at %d km (second difference %.3f)" % (COLUMNS[case], series[i][0], second))
    return found


def render_header(g4: List[Row4]) -> str:
    lines = [
        "#pragma once",
        "",
        "// GENERATED by tools/ecss_table.py from data/ecss_e_st_10_04c_table_g4.txt. Do not edit by hand: change the data file and",
        "// run `python tools/ecss_table.py generate`.",
        "//",
        "// Total air density from the JB-2006 model for four levels of solar and geomagnetic activity, as printed in Table G-4 of",
        "// ECSS-E-ST-10-04C (Space engineering, Space environment, 15 November 2008, Annex G), at equatorial latitude and averaged",
        "// over diurnal and seasonal variations, from 100 km to 900 km in 41 rows 20 km apart, each density to three significant",
        "// figures as printed:",
        "//   low:               F10.7 = 65,  Ap = 0",
        "//   moderate:          F10.7 = 140, Ap = 15",
        "//   high, long term:   F10.7 = 250, Ap = 45",
        "//   high, short term:  F10.7 = 300 (81-day average 250), Ap = 240",
        "// The standard says JB-2006 is valid from 120 km; the rows at 100 km are printed in the table and are kept. How the",
        "// numbers were read and checked, and what they cannot tell you, is in docs/ATMOSPHERE.md and docs/VALIDATION.md.",
        "// Copyright 2008 by the European Space Agency for the members of ECSS; the numbers are reproduced with attribution.",
        "",
        "#include <cstddef>",
        "#include <utility>",
        "#include <vector>",
        "",
        '#include "abep/atmosphere.hpp"',
        "",
        "namespace abep {",
        "namespace ecss {",
        "",
        "enum class Activity { Low = 0, Moderate = 1, High = 2, HighShortTerm = 3 };",
        "",
        "inline constexpr std::size_t kActivityCount = 4;",
        "",
        "struct Row {",
        "    double altitude_m;",
        "    double density_kg_m3[kActivityCount];  // in the order of Activity",
        "};",
        "",
        "// {geometric altitude [m], {low, moderate, high long term, high short term} density [kg/m^3]}, in increasing altitude.",
        "inline constexpr Row kRows[] = {",
    ]
    for r in g4:
        lines.append("    {%d.0, {%s}}," % (r.h_km * 1000, ", ".join(t.lower() for t in r.text)))
    lines += [
        "};",
        "",
        "inline constexpr std::size_t kRowCount = sizeof(kRows) / sizeof(kRows[0]);",
        "",
        "inline const char* name(Activity activity) {",
        "    switch (activity) {",
        '        case Activity::Low: return "ECSS low solar activity (JB-2006, F10.7 = 65, Ap = 0)";',
        '        case Activity::Moderate: return "ECSS moderate solar activity (JB-2006, F10.7 = 140, Ap = 15)";',
        '        case Activity::High: return "ECSS high long-term solar activity (JB-2006, F10.7 = 250, Ap = 45)";',
        '        case Activity::HighShortTerm: return "ECSS high short-term solar activity (JB-2006, F10.7 = 300, Ap = 240)";',
        "    }",
        '    return "unknown";',
        "}",
        "",
        "// One activity level as a density profile. The rows are 20 km apart, too far for the log-linear rule of the 1976 standard",
        "// (docs/VALIDATION.md, case 31), so it interpolates with a monotone cubic of ln(density). Throws std::out_of_range outside",
        "// 100 km to 900 km. Build it once and keep it; constructing it copies the rows.",
        "inline DensityTable density_table(Activity activity) {",
        "    std::vector<DensityTable::Node> nodes;",
        "    nodes.reserve(kRowCount);",
        "    for (std::size_t i = 0; i < kRowCount; ++i) {",
        "        nodes.push_back({kRows[i].altitude_m, kRows[i].density_kg_m3[static_cast<std::size_t>(activity)]});",
        "    }",
        "    return DensityTable(std::move(nodes), DensityTable::Interpolation::MonotoneCubic);",
        "}",
        "",
        "}  // namespace ecss",
        "}  // namespace abep",
        "",
    ]
    return "\n".join(lines)


def main(argv: List[str]) -> int:
    if len(argv) != 2 or argv[1] not in ("check", "generate"):
        sys.stderr.write(__doc__ + "\n")
        return 2
    g4 = read_g4(G4)
    g123 = read_g123(G123)
    found = problems(g4, g123)
    if found:
        for message in found:
            sys.stderr.write("PROBLEM: %s\n" % message)
        return 1
    text = render_header(g4)
    if argv[1] == "generate":
        with open(HEADER, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        print("wrote %s (%d rows)" % (HEADER.relative_to(ROOT), len(g4)))
        return 0
    current = HEADER.read_text(encoding="utf-8").replace("\r\n", "\n") if HEADER.exists() else None
    if current != text:
        sys.stderr.write("PROBLEM: %s does not match %s; run `python tools/ecss_table.py generate`\n"
                         % (HEADER.relative_to(ROOT), G4.relative_to(ROOT)))
        return 1
    print("ok: %d rows of Table G-4 and %d of Tables G-1 to G-3; spacing, monotonic fall, ordering by activity, the ratio of the"
          " two models and the header all pass" % (len(g4), len(g123)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
