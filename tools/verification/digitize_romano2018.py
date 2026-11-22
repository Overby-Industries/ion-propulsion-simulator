#!/usr/bin/env python3
"""Read three published curves from the vector figures of F. Romano, B. Massuti-Ballester, T. Binder, G. Herdrich,
S. Fasoulas and T. Schoenherr, "System analysis and test-bed for an atmosphere-breathing electric propulsion system
using an inductive plasma thruster", Acta Astronautica 147 (2018) 114-126, doi:10.1016/j.actaastro.2018.03.031, and
write them as a small table.

    python tools/verification/digitize_romano2018.py PATH/TO/paper.pdf [--write data/romano2018_curves.csv]

  Fig. 3 (PDF page 4, top):    NRLMSISE-00 density against altitude for three solar-activity cases. The middle one,
                               F10.7 = 140 (the average), Ap = 15, is taken. The curves are told apart by line width,
                               thickest to thinnest in the legend's order: 250, 140, 65.
  Fig. 5 (PDF page 4, bottom): collectable mass flow against altitude for A = 1 m^2 and collection efficiencies 0.35,
                               0.46 and 0.90, in the same average atmosphere (colours blue, orange, yellow).
  Fig. 6 (PDF page 5):         drag against altitude for a frontal area of 1 m^2 in that atmosphere (their Eq. 1,
                               C_D = 2.2).

All three are vector drawings, so the curves are read from the drawing coordinates, not by eye. The axes are
calibrated from the major tick marks drawn in the same PDF, and the script stops if they do not fall where they
should. Needs pymupdf. The PDF is untrusted data and is only parsed for coordinates. An open copy of the paper was at
https://discoverer.space/wp-content/uploads/2021/07/1-s2.0-S009457651730406X-main.pdf when this was written."""
import math
import sys

import pymupdf

MU = 3.986004418e14       # WGS 84 GM, m^3/s^2 (used only for the consistency print-out at the end)
RE = 6378137.0            # WGS 84 equatorial radius, m
W = 7.292115e-5           # WGS 84 rotation rate, rad/s
CD, AREA = 2.2, 1.0       # the paper's drag coefficient and frontal area
ETAS = (0.35, 0.46, 0.90)


def plot_frame(page, top_half):
    """The white rectangle behind a plot. `top_half` picks the upper or the lower one on a page with two."""
    frames = [d["rect"] for d in page.get_drawings()
              if d.get("fill") == (1.0, 1.0, 1.0) and d.get("color") is None and d["rect"].width > 250
              and d["rect"].height > 100]
    frames.sort(key=lambda r: r.y0)
    return frames[0] if top_half else frames[-1]


def major_ticks(page, frame):
    """Positions of the major tick marks (length 2.9 pt) on the left and bottom edges of the frame."""
    left, bottom = [], []
    for d in page.get_drawings():
        r, col = d["rect"], d.get("color")
        if col is None or abs(col[0] - 0.149) > 0.01 or len(d["items"]) != 1:
            continue
        if abs(r.x0 - frame.x0) < 0.6 and r.height < 0.01 and 2.0 < r.width < 4.0:
            left.append(r.y0)
        if abs(r.y1 - frame.y1) < 0.6 and r.width < 0.01 and 2.0 < r.height < 4.0:
            bottom.append(r.x0)
    return sorted(set(round(y, 3) for y in left), reverse=True), sorted(set(round(x, 3) for x in bottom))


class Axes:
    """Maps PDF coordinates to (altitude in km, value) using the frame edges for x and the major ticks for y."""

    def __init__(self, page, frame, x_range, decades, label):
        left, bottom = major_ticks(page, frame)
        if len(left) != len(decades):
            raise RuntimeError("%s: expected %d major y ticks, found %d" % (label, len(decades), len(left)))
        steps = [a - b for a, b in zip(left, left[1:])]
        if max(steps) - min(steps) > 0.02:
            raise RuntimeError("%s: the major y ticks are not evenly spaced: %s" % (label, steps))
        self.frame, self.x_range = frame, x_range
        self.y_bottom, self.decade0, self.per_decade = left[0], decades[0], sum(steps) / len(steps)
        worst = max(abs(self.altitude(x) - round(self.altitude(x) / 10.0) * 10.0) for x in bottom)
        if worst > 0.05:
            raise RuntimeError("%s: bottom ticks are not at multiples of 10 km (worst %.3f km)" % (label, worst))
        print("%s: %d major y ticks %.3f pt per decade; %d bottom ticks, worst distance from a multiple of 10 km %.4f km"
              % (label, len(left), self.per_decade, len(bottom), worst))

    def altitude(self, x):
        lo, hi = self.x_range
        return lo + (x - self.frame.x0) / (self.frame.x1 - self.frame.x0) * (hi - lo)

    def value(self, y):
        return 10.0 ** (self.decade0 + (self.y_bottom - y) / self.per_decade)


def read_curve(drawing, axes):
    pts = []
    for item in drawing["items"]:
        if item[0] == "l":
            for p in item[1:3]:
                if not pts or abs(p.x - pts[-1][0]) > 1e-6 or abs(p.y - pts[-1][1]) > 1e-6:
                    pts.append((p.x, p.y))
    curve = sorted((axes.altitude(x), axes.value(y)) for x, y in pts)
    return curve


def at(curve, h):
    """Log-linear interpolation; the end points are used if h is within 0.05 km outside the plotted range."""
    if h < curve[0][0] and curve[0][0] - h < 0.05:
        return curve[0][1]
    if h > curve[-1][0] and h - curve[-1][0] < 0.05:
        return curve[-1][1]
    for (ha, fa), (hb, fb) in zip(curve, curve[1:]):
        if ha <= h <= hb:
            w = (h - ha) / (hb - ha) if hb > ha else 0.0
            return math.exp(math.log(fa) + w * (math.log(fb) - math.log(fa)))
    raise ValueError(h)


def long_paths(page, predicate):
    return [d for d in page.get_drawings() if d.get("color") is not None and len(d["items"]) > 40 and predicate(d)]


def main(pdf_path, out_csv):
    doc = pymupdf.open(pdf_path)

    page = doc[4]   # Fig. 6
    ax6 = Axes(page, plot_frame(page, top_half=False), (80.0, 250.0), list(range(0, 7)), "Fig. 6")
    blue = long_paths(page, lambda d: d["color"][2] > 0.5 and d["color"][0] < 0.1)
    drag = read_curve(blue[0], ax6)

    page = doc[3]   # Fig. 3 (upper plot) and Fig. 5 (lower plot)
    ax3 = Axes(page, plot_frame(page, top_half=True), (120.0, 250.0), list(range(-11, -6)), "Fig. 3")
    blacks = sorted(long_paths(page, lambda d: d["color"] == (0.0, 0.0, 0.0) and d["rect"].y0 < 300), key=lambda d: -d["width"])
    if len(blacks) != 3:
        raise RuntimeError("expected three black density curves, found %d" % len(blacks))
    density = {k: read_curve(d, ax3) for k, d in zip(("250", "140", "65"), blacks)}
    print("Fig. 3 line widths (thickest first): %s -> F10.7 = 250, 140, 65" % [round(d["width"], 3) for d in blacks])
    if not density["250"][-1][1] > density["140"][-1][1] > density["65"][-1][1]:
        raise RuntimeError("curve identification by line width disagrees with the solar-activity ordering")

    ax5 = Axes(page, plot_frame(page, top_half=False), (120.0, 250.0), list(range(-1, 3)), "Fig. 5")
    colours = {0.35: (0.0, 0.447, 0.741), 0.46: (0.851, 0.325, 0.098), 0.90: (0.929, 0.694, 0.125)}
    mass_flow = {}
    for eta, col in colours.items():
        found = long_paths(page, lambda d, c=col: d["rect"].y0 > 500 and all(abs(a - b) < 0.01 for a, b in zip(d["color"], c)))
        if len(found) != 1:
            raise RuntimeError("expected one mass-flow curve of colour %s, found %d" % (col, len(found)))
        mass_flow[eta] = read_curve(found[0], ax5)

    rows = []
    for h in range(120, 251, 10):
        rows.append((h, at(density["140"], h), at(drag, h), [at(mass_flow[e], h) for e in ETAS]))

    print("\nConsistency with the formulas, using the density read from Fig. 3 (a ratio of 1.000 is exact agreement).")
    print("v_rel = v_circ - w*R_E reproduces the paper; v_rel = v_circ - w*r is what the simulator uses for a co-rotating")
    print("atmosphere in an equatorial orbit; v_rel = v_circ is a non-rotating atmosphere.")
    print("  h km  drag/F(wRE)  drag/F(w r)  drag/F(none)   mass flow / formula, v_rel = v - w*R_E, eta = %s" % (ETAS,))
    for h, rho, f, m in rows:
        r = RE + h * 1e3
        v = math.sqrt(MU / r)
        force = lambda vrel: 0.5 * rho * vrel * vrel * CD * AREA * 1e3          # mN
        flow = lambda e: e * rho * (v - W * RE) * AREA * 1e6                    # mg/s
        print("  %3d   %.4f       %.4f       %.4f         %s" % (
            h, f / force(v - W * RE), f / force(v - W * r), f / force(v),
            "  ".join("%.4f" % (mi / flow(e)) for mi, e in zip(m, ETAS))))

    if out_csv:
        with open(out_csv, "w", encoding="utf-8", newline="\n") as f:
            f.write("# Curves read from the vector figures of F. Romano, B. Massuti-Ballester, T. Binder, G. Herdrich,\n")
            f.write("# S. Fasoulas, T. Schoenherr, Acta Astronautica 147 (2018) 114-126, doi:10.1016/j.actaastro.2018.03.031.\n")
            f.write("# density_kg_m3: Fig. 3, NRLMSISE-00, F10.7 = F10.7avg = 140, Ap = 15 (average solar activity).\n")
            f.write("# drag_mN: Fig. 6, A_f = 1.0 m^2, same atmosphere (their Eq. 1, C_D = 2.2).\n")
            f.write("# mass_flow_mg_s_*: Fig. 5, A_in = A_f = 1.0 m^2, same atmosphere, collection efficiency 0.35, 0.46, 0.90.\n")
            f.write("# Read by tools/verification/digitize_romano2018.py from the drawing coordinates; log-linear\n")
            f.write("# interpolation between the plotted points. Altitude is geometric altitude as plotted.\n")
            f.write("altitude_km,density_kg_m3,drag_mN,mass_flow_mg_s_eta_0.35,mass_flow_mg_s_eta_0.46,mass_flow_mg_s_eta_0.90\n")
            for h, rho, d, m in rows:
                f.write("%d,%.5g,%.5g,%.5g,%.5g,%.5g\n" % (h, rho, d, m[0], m[1], m[2]))
        print("\nwrote", out_csv)


if __name__ == "__main__":
    args = sys.argv[1:]
    out = None
    if "--write" in args:
        i = args.index("--write")
        out = args[i + 1]
        del args[i:i + 2]
    if len(args) != 1:
        sys.exit(__doc__)
    main(args[0], out)
