#!/usr/bin/env python3
"""Reread Tables G-1 to G-4 of ECSS-E-ST-10-04C from the standard's PDF and compare them with the data files.

    python tools/verification/extract_ecss_tables.py ECSS-E-ST-10-04C15November2008.pdf [--write DIR]

Needs PyMuPDF (`pip install pymupdf`) and the PDF, which is free from
https://ecss.nl/wp-content/uploads/standards/ecss-e/ECSS-E-ST-10-04C15November2008.pdf . Not run in the CI. The PDF has a text layer,
so the numbers are read from it and not from the page images; the data files in data/ were also compared by eye with the rendered
pages (docs/ATMOSPHERE.md). The script prints whether the rows it reads equal the rows of
data/ecss_e_st_10_04c_table_g4.txt and data/ecss_e_st_10_04c_tables_g1_g3.txt (exit status 1 if they do not), and with --write DIR it
writes what it read into DIR, in the data files' column layout and without their header comments.
"""
import importlib.util
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
HYPHENS = "‐‑‒–−"  # the standard prints a non-breaking hyphen, even in the exponent
NUMBER = re.compile(r"^\d\.\d\dE[+-]\d{1,2}$")  # one cell of Table G-3 (60 km) prints 3,64E-4, one digit of exponent
INTEGER = re.compile(r"^\d{1,3}$")


def normalise(token):
    for hyphen in HYPHENS:
        token = token.replace(hyphen, "-")
    return token.replace(",", ".")


def rows_of(tokens, width):
    """(H, [width numbers]) wherever an integer is followed by exactly `width` numbers in E notation."""
    rows, j = [], 0
    while j < len(tokens):
        if INTEGER.match(tokens[j]) and all(NUMBER.match(t) for t in tokens[j + 1 : j + 1 + width]) and len(tokens[j + 1 : j + 1 + width]) == width:
            following = tokens[j + 1 + width] if j + 1 + width < len(tokens) else ""
            if not NUMBER.match(following):
                rows.append((int(tokens[j]), tokens[j + 1 : j + 1 + width]))
                j += 1 + width
                continue
        j += 1
    return rows


def read_tables(pdf):
    try:
        import pymupdf as fitz
    except ImportError:
        import fitz
    lines = []
    for page in fitz.open(str(pdf)):
        lines.extend(line.strip() for line in page.get_text().split("\n"))
    g9 = max(i for i, line in enumerate(lines) if re.match(r"^G\.9\s+Tables", line))
    g10 = next(i for i in range(g9, len(lines)) if re.match(r"^G\.10\s+Figures", lines[i]))
    captions = {}
    for i in range(g9, g10):
        match = re.match(r"^Table\sG\W(\d)\s*:", lines[i])
        if match and match.group(1) not in captions:
            captions[match.group(1)] = i
    bounds = [captions["1"], captions["2"], captions["3"], captions["4"], g10]
    tables = {}
    for k in range(4):
        tokens = [normalise(line) for line in lines[bounds[k] : bounds[k + 1]] if line]
        tables[k + 1] = rows_of(tokens, 10 if k < 3 else 4)
    return tables


def main(argv):
    arguments = [a for a in argv[1:] if not a.startswith("--")]
    write_to = None
    if "--write" in argv:
        write_to = Path(argv[argv.index("--write") + 1])
        arguments = [a for a in arguments if a != str(write_to)]
    if len(arguments) != 1:
        sys.stderr.write(__doc__ + "\n")
        return 2
    spec = importlib.util.spec_from_file_location("ecss_table", ROOT / "tools" / "ecss_table.py")
    ecss_table = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(ecss_table)
    tables = read_tables(arguments[0])
    problems = []

    g4 = tables[4]
    if [h for h, _ in g4] != list(range(100, 901, 20)):
        problems.append("Table G-4: expected 41 rows from 100 to 900 km, read %d" % len(g4))
    expected_g4 = ecss_table.read_g4(ecss_table.G4)
    for (h, values), row in zip(g4, expected_g4):
        if h != row.h_km or [float(v) for v in values] != row.rho:
            problems.append("Table G-4 at %d km: the PDF says %s, the data file %s" % (h, " ".join(values), " ".join(row.text)))
    if len(g4) != len(expected_g4):
        problems.append("Table G-4: the PDF has %d rows, the data file %d" % (len(g4), len(expected_g4)))

    expected_g123 = ecss_table.read_g123(ecss_table.G123)
    for k in (1, 2, 3):
        rows = tables[k]
        if [h for h, _ in rows] != list(range(0, 901, 20)):
            problems.append("Table G-%d: expected 46 rows from 0 to 900 km, read %d" % (k, len(rows)))
            continue
        for (h, values), row in zip(rows, expected_g123):
            temperature, rho = float(values[8]), float(values[9])  # the last two of ten: T (K) and rho (kg/m^3)
            if h != row.h_km or temperature != row.temperature_k[k - 1] or rho != row.rho[k - 1]:
                problems.append("Table G-%d at %d km: the PDF says T %s rho %s, the data file T %g rho %g"
                                % (k, h, values[8], values[9], row.temperature_k[k - 1], row.rho[k - 1]))
    if len(expected_g123) != 46:
        problems.append("the data file of Tables G-1 to G-3 has %d rows, expected 46" % len(expected_g123))

    if write_to is not None:
        write_to.mkdir(parents=True, exist_ok=True)
        with open(write_to / "g4_jb2006.txt", "w", encoding="utf-8", newline="\n") as f:
            for h, values in g4:
                f.write("%d %s\n" % (h, " ".join(values)))
        with open(write_to / "g123_nrlmsise00.txt", "w", encoding="utf-8", newline="\n") as f:
            for h in range(0, 901, 20):
                cells = []
                for k in (1, 2, 3):
                    values = dict(tables[k])[h]
                    cells.append("%.0f %.2E" % (float(values[8]), float(values[9])))
                f.write("%d %s\n" % (h, " ".join(cells)))
        print("wrote the rows it read into %s" % write_to)
    if problems:
        for message in problems:
            sys.stderr.write("DIFFERENCE: %s\n" % message)
        return 1
    print("identical: %d rows of Table G-4 and %d rows each of Tables G-1 to G-3 in the PDF equal the data files" % (len(g4), len(tables[1])))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
