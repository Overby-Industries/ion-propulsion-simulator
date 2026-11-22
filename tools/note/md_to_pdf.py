#!/usr/bin/env python3
"""Build a PDF from a small Markdown document, using Microsoft Edge or Google Chrome to print it.

    python tools/note/md_to_pdf.py docs/TECHNICAL_NOTE.md docs/TECHNICAL_NOTE.pdf

Standard library only. It understands the subset of Markdown the technical note is written in:
# ## ### headings, paragraphs, - and 1. lists, pipe tables (with :--- and ---: alignment), ``` code
blocks, `code`, **bold**, *italic* and [text](url). Links that are not absolute are made into links
to the file on GitHub, so that they work in the PDF. Needs Edge or Chrome; set BROWSER to the path
of one to choose.
"""

import html
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_URL = "https://github.com/Overby-Industries/ion-propulsion-simulator/blob/main/"

CSS = """
@page { size: A4; margin: 20mm 18mm 20mm 18mm;
        @bottom-center { content: counter(page) " / " counter(pages); font: 8pt 'Segoe UI', Arial, sans-serif; color: #667085; } }
html { font-size: 10.5pt; }
body { font-family: Cambria, Georgia, 'Times New Roman', serif; line-height: 1.38; color: #1b1f24; margin: 0; }
h1 { font-family: 'Segoe UI', Arial, sans-serif; font-size: 19pt; line-height: 1.2; margin: 0 0 5pt; }
h1 + p { font-family: 'Segoe UI', Arial, sans-serif; font-size: 9pt; color: #55606c; margin: 0 0 10pt; }
h2 { font-family: 'Segoe UI', Arial, sans-serif; font-size: 13pt; margin: 15pt 0 5pt; padding-bottom: 2pt;
     border-bottom: 1px solid #cfd6df; break-after: avoid; }
h3 { font-family: 'Segoe UI', Arial, sans-serif; font-size: 11pt; margin: 11pt 0 4pt; break-after: avoid; }
p { margin: 0 0 7pt; }
ul, ol { margin: 0 0 7pt 0; padding-left: 18pt; }
li { margin-bottom: 3pt; }
code { font-family: Consolas, 'Courier New', monospace; font-size: 0.88em; background: #f1f3f6; padding: 0 2px; border-radius: 2px; }
pre { background: #f1f3f6; border: 1px solid #dde2e8; padding: 6pt 8pt; font-size: 8.8pt; line-height: 1.3;
      white-space: pre-wrap; break-inside: avoid; margin: 0 0 8pt; }
pre code { background: none; padding: 0; font-size: inherit; }
table { border-collapse: collapse; width: 100%; margin: 4pt 0 9pt; font-family: 'Segoe UI', Arial, sans-serif;
        font-size: 8.3pt; line-height: 1.3; break-inside: avoid; }
th, td { border: 1px solid #cfd6df; padding: 2.5pt 5pt; vertical-align: top; }
th { background: #eaeef3; text-align: left; }
a { color: #1a4f8b; text-decoration: none; }
"""


def link_target(href, source_dir, repo_root):
    """Absolute links stay; a relative one becomes a link to the file on GitHub."""
    if re.match(r"^[a-z][a-z0-9+.-]*:", href, re.I) or href.startswith("#"):
        return href
    try:
        relative = (source_dir / href).resolve().relative_to(repo_root.resolve())
    except ValueError:
        return href
    return REPO_URL + relative.as_posix()


def inline(text, source_dir, repo_root):
    out = []
    for part in re.split(r"(`[^`]+`)", text):
        if len(part) > 1 and part.startswith("`") and part.endswith("`"):
            out.append("<code>" + html.escape(part[1:-1], quote=False) + "</code>")
            continue
        s = html.escape(part, quote=False)
        s = re.sub(
            r"\[([^\]]+)\]\(([^)\s]+)\)",
            lambda m: '<a href="%s">%s</a>' % (link_target(m.group(2), source_dir, repo_root), m.group(1)),
            s,
        )
        s = re.sub(r"\*\*(.+?)\*\*", r"<strong>\1</strong>", s)
        s = re.sub(r"(?<![\w*])\*(?![\s*])(.+?)(?<![\s*])\*(?![\w*])", r"<em>\1</em>", s)
        out.append(s)
    return "".join(out)


def split_row(row):
    return [c.strip() for c in row.strip().strip("|").split("|")]


def render_table(rows, fmt):
    header = split_row(rows[0])
    aligns = []
    for cell in split_row(rows[1]):
        if cell.endswith(":") and cell.startswith(":"):
            aligns.append("center")
        elif cell.endswith(":"):
            aligns.append("right")
        else:
            aligns.append("left")
    parts = ["<table><thead><tr>"]
    for i, cell in enumerate(header):
        parts.append('<th style="text-align:%s">%s</th>' % (aligns[i], fmt(cell)))
    parts.append("</tr></thead><tbody>")
    for row in rows[2:]:
        parts.append("<tr>")
        for i, cell in enumerate(split_row(row)):
            parts.append('<td style="text-align:%s">%s</td>' % (aligns[min(i, len(aligns) - 1)], fmt(cell)))
        parts.append("</tr>")
    parts.append("</tbody></table>")
    return "".join(parts)


def render(markdown, source_dir, repo_root):
    def fmt(t):
        return inline(t, source_dir, repo_root)

    lines = markdown.replace("\r\n", "\n").split("\n")
    out, title, i = [], None, 0
    block_start = re.compile(r"^(#{1,3} |```|\||\s*[-*] |\d+\. )")
    while i < len(lines):
        line = lines[i]
        if not line.strip():
            i += 1
            continue
        if line.startswith("```"):
            j, buf = i + 1, []
            while j < len(lines) and not lines[j].startswith("```"):
                buf.append(lines[j])
                j += 1
            out.append("<pre><code>" + html.escape("\n".join(buf), quote=False) + "</code></pre>")
            i = j + 1
            continue
        m = re.match(r"^(#{1,3}) (.*)$", line)
        if m:
            level, text = len(m.group(1)), m.group(2).strip()
            if level == 1 and title is None:
                title = text
            out.append("<h%d>%s</h%d>" % (level, fmt(text), level))
            i += 1
            continue
        if line.startswith("|"):
            rows = []
            while i < len(lines) and lines[i].startswith("|"):
                rows.append(lines[i])
                i += 1
            out.append(render_table(rows, fmt))
            continue
        m = re.match(r"^\s*[-*] (.*)$", line) or re.match(r"^\d+\. (.*)$", line)
        if m:
            ordered = bool(re.match(r"^\d+\. ", line))
            item_re = re.compile(r"^\d+\. (.*)$" if ordered else r"^\s*[-*] (.*)$")
            items = []
            while i < len(lines):
                mi = item_re.match(lines[i])
                if mi:
                    items.append(mi.group(1))
                    i += 1
                elif lines[i].startswith("  ") and lines[i].strip() and items:
                    items[-1] += " " + lines[i].strip()
                    i += 1
                else:
                    break
            tag = "ol" if ordered else "ul"
            out.append("<%s>%s</%s>" % (tag, "".join("<li>%s</li>" % fmt(t) for t in items), tag))
            continue
        para = [line.strip()]
        i += 1
        while i < len(lines) and lines[i].strip() and not block_start.match(lines[i]):
            para.append(lines[i].strip())
            i += 1
        out.append("<p>%s</p>" % fmt(" ".join(para)))
    return title or "Document", "\n".join(out)


def find_browser():
    chosen = os.environ.get("BROWSER")
    if chosen and Path(chosen).exists():
        return chosen
    for candidate in (
        r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
        r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
        r"C:\Program Files\Google\Chrome\Application\chrome.exe",
        r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
    ):
        if Path(candidate).exists():
            return candidate
    for name in ("msedge", "microsoft-edge", "google-chrome", "chromium", "chrome"):
        found = shutil.which(name)
        if found:
            return found
    raise SystemExit("No Edge or Chrome found. Set BROWSER to the path of one.")


def main(argv):
    if len(argv) != 3:
        raise SystemExit(__doc__)
    source, target = Path(argv[1]), Path(argv[2])
    repo_root = Path(__file__).resolve().parent.parent.parent
    title, body = render(source.read_text(encoding="utf-8"), source.resolve().parent, repo_root)
    page = (
        '<!doctype html><html lang="en"><head><meta charset="utf-8"><title>%s</title><style>%s</style></head>'
        "<body>%s</body></html>" % (html.escape(title), CSS, body)
    )
    with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as tmp:
        page_path = Path(tmp) / "note.html"
        page_path.write_text(page, encoding="utf-8")
        printed = Path(tmp) / "note.pdf"
        command = [
            find_browser(),
            "--headless",
            "--disable-gpu",
            "--no-pdf-header-footer",
            "--user-data-dir=" + str(Path(tmp) / "profile"),
            "--print-to-pdf=" + str(printed),
            page_path.as_uri(),
        ]
        try:
            subprocess.run(command, timeout=120, check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except subprocess.TimeoutExpired:
            pass
        if not printed.exists() or printed.stat().st_size == 0:
            raise SystemExit("The browser did not write a PDF.")
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(printed, target)
    print("wrote %s (%d bytes)" % (target, target.stat().st_size))


if __name__ == "__main__":
    main(sys.argv)
