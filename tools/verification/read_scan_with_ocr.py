#!/usr/bin/env python3
"""Cross-check data/ussa1976_table1.txt against the scanned report with an independent machine read (OCR).

    python tools/verification/read_scan_with_ocr.py PATH/TO/19770009539.pdf

The report is NASA-TM-X-74335, NTRS 19770009539. Table I for 100 km to 1000 km is on PDF pages 83 to 88. The
file has no usable text layer, so the data file was first read by eye; this script reads the same page images with
an OCR engine and counts how many of the 802 printed numbers (401 rows, two columns) it reads identically.

Needs pymupdf, pillow, numpy and rapidocr-onnxruntime (pip install pymupdf pillow rapidocr-onnxruntime). Not run
in CI. Whatever the OCR misses or misreads is listed and has to be checked by eye against the report. The result
this script gave, and how the misses were resolved, is recorded in docs/ATMOSPHERE.md.
"""
import io
import re
import sys
from pathlib import Path

import numpy as np
import pymupdf
from PIL import Image
from rapidocr_onnxruntime import RapidOCR

DATA = Path(__file__).resolve().parent.parent.parent / "data" / "ussa1976_table1.txt"
PAGES = range(83, 89)             # PDF pages holding Table I from 100 km to 1000 km
DPI = 300
SCALE = DPI / 200.0               # the crop positions below were measured at 200 dpi
LEFT = (int(90 * SCALE), int(330 * SCALE))      # the geometric-altitude column (and a sliver of its neighbour)
RIGHT = (int(1130 * SCALE), int(1695 * SCALE))  # density and density-ratio columns
GAP = int(24 * SCALE)
STRIP_HEIGHT, OVERLAP, TOP = 900, 130, int(60 * SCALE)
Z_PART_END = (LEFT[1] - LEFT[0]) - 10   # x below which a box belongs to the Z side of a strip
ROW_TOLERANCE = 12.0                    # px; the row pitch is about 26 px
MANTISSA = re.compile(r"^(\d\.\d{3})")


def read_data_file():
    """Rows as printed: z -> (rho token, ratio token), tokens exactly as written in the data file."""
    rows = {}
    for line in DATA.read_text(encoding="utf-8").splitlines():
        if line.strip() and not line.startswith("#"):
            z, rho, ratio = line.split()
            rows[int(z)] = (rho, ratio)
    return rows


def strips(pdf_path):
    document = pymupdf.open(pdf_path)
    for page in PAGES:
        image = Image.open(io.BytesIO(document[page - 1].get_pixmap(dpi=DPI).tobytes("png"))).convert("RGB")
        top = TOP
        while top < image.height:
            bottom = min(top + STRIP_HEIGHT, image.height)
            left = image.crop((LEFT[0], top, LEFT[1], bottom))
            right = image.crop((RIGHT[0], top, RIGHT[1], bottom))
            strip = Image.new("RGB", (left.width + GAP + right.width, bottom - top), "white")
            strip.paste(left, (0, 0))
            strip.paste(right, (left.width + GAP, 0))
            yield "p%d@%d" % (page, top), strip
            if bottom == image.height:
                break
            top = bottom - OVERLAP


def normalise(text):
    return re.sub(r"\s+", "", text.replace("−", "-").replace("–", "-"))


def rows_in_strip(engine, strip):
    """OCR one strip into rows of (z text, rho text, ratio text). A row is anchored by its d.ddd boxes; exponent
    pieces ("-", "7", "10") go with the nearest d.ddd box to their left."""
    result, _ = engine(np.array(strip), use_cls=False)
    boxes = [dict(x0=min(p[0] for p in b), x1=max(p[0] for p in b), y=sum(p[1] for p in b) / 4.0, text=normalise(t))
             for b, t, _ in (result or [])]
    mantissas = sorted([b for b in boxes if b["x0"] > Z_PART_END and MANTISSA.match(b["text"])], key=lambda b: b["y"])
    groups = []
    for b in mantissas:
        if groups and abs(b["y"] - groups[-1][-1]["y"]) <= ROW_TOLERANCE:
            groups[-1].append(b)
        else:
            groups.append([b])
    for g in groups:
        g.sort(key=lambda b: b["x0"])
    # Where the two columns sit in this strip, from the rows in which both numbers were found.
    pairs = [g for g in groups if len(g) >= 2]
    if not pairs:
        return []
    column_x = (float(np.median([g[0]["x0"] for g in pairs])), float(np.median([g[1]["x0"] for g in pairs])))
    rows = []
    for g in groups:
        y = sum(b["y"] for b in g) / len(g)
        if len(g) == 1:   # one number found on the row: it belongs to whichever column it is nearest
            nearest = int(abs(g[0]["x0"] - column_x[1]) < abs(g[0]["x0"] - column_x[0]))
            g = [g[0], None] if nearest == 0 else [None, g[0]]
        else:
            g = g[:2]
        own = [b for b in g if b is not None]
        row_boxes = [b for b in boxes if abs(b["y"] - y) <= ROW_TOLERANCE and b["x0"] > Z_PART_END and b not in own]
        texts = []
        for k, anchor in enumerate(g):
            if anchor is None:
                texts.append("")
                continue
            nxt = g[1] if k == 0 else None
            pieces = [anchor] + [b for b in row_boxes if b["x0"] > anchor["x0"] and (nxt is None or b["x0"] < nxt["x0"])]
            texts.append("".join(b["text"] for b in sorted(pieces, key=lambda b: b["x0"])))
        z_boxes = sorted([b for b in boxes if abs(b["y"] - y) <= ROW_TOLERANCE and b["x1"] <= Z_PART_END],
                         key=lambda b: b["x0"])
        rows.append((z_boxes[0]["text"] if z_boxes else "", texts[0], texts[1]))
    return rows


def main(pdf_path):
    printed = read_data_file()
    altitudes = sorted(printed)
    engine = RapidOCR()
    reads = {}   # z -> list of (rho text, ratio text), one per strip the row appeared in
    for name, strip in strips(pdf_path):
        rows = rows_in_strip(engine, strip)
        known = []
        for i, (z_text, _, _) in enumerate(rows):
            for width in (7, 6):   # a stray digit of the neighbouring column can trail the altitude
                if z_text[:width].isdigit() and len(z_text) >= width and int(z_text[:width]) in printed:
                    known.append((i, int(z_text[:width])))
                    break
        for i, (_, rho, ratio) in enumerate(rows):
            if not known:
                continue
            j, z_known = min(known, key=lambda k: abs(k[0] - i))
            k = altitudes.index(z_known) + (i - j)   # the table is contiguous, so count rows from a row that was read
            if 0 <= k < len(altitudes):
                reads.setdefault(altitudes[k], []).append((rho, ratio))
        print("  %s: %d rows" % (name, len(rows)), flush=True)

    unconfirmed = []
    explicit = agree_exponent = disagree_exponent = 0
    confirmed = [0, 0]
    for z in altitudes:
        for column, name in enumerate(("rho", "rho/rho0")):
            token = printed[z][column]
            wanted = MANTISSA.match(token).group(1)
            texts = [r[column] for r in reads.get(z, []) if MANTISSA.match(r[column])]
            if any(MANTISSA.match(t).group(1) == wanted for t in texts):
                confirmed[column] += 1
            else:
                unconfirmed.append((z, name, token, texts))
            if "-" in token:   # an exponent printed on the page
                explicit += 1
                seen = [re.search(r"-(\d+)$", r[column]) for r in reads.get(z, [])]
                seen = [m.group(1) for m in seen if m]
                if any(s == token.split("-")[1] for s in seen):
                    agree_exponent += 1
                elif seen:
                    disagree_exponent += 1
    n = len(altitudes)
    print()
    print("rows in the data file: %d; rows the OCR read at least once: %d" % (n, len(reads)))
    print("numbers (d.ddd) the OCR read identically: rho %d of %d, rho/rho0 %d of %d" % (confirmed[0], n, confirmed[1], n))
    print("printed exponents in the data file: %d; OCR agrees on %d, disagrees on %d, did not capture %d"
          % (explicit, agree_exponent, disagree_exponent, explicit - agree_exponent - disagree_exponent))
    print("not confirmed by the OCR (%d); check each by eye in the report:" % len(unconfirmed))
    for z, name, token, texts in unconfirmed:
        print("  %7d m  %-8s data file %-9s OCR %s" % (z, name, token, texts if texts else "(nothing usable)"))


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
