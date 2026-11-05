#!/usr/bin/env python3
"""Regenerate the index in docs/adr/README.md from the records' headers.

Each record starts with a title line and a bullet header:

    # Title
    - **Status:** Accepted
    - **Area:** hardware
    - **First decided:** 2026-11-03 · **Last changed:** 2026-11-04

The block between `<!-- index:start -->` and `<!-- index:end -->` in
README.md is rewritten: the by-area lists first, then the by-date table.
Run from anywhere: `python3 tools/adr_index.py` (also `make adr-index`).
Exits non-zero if a record's header is missing or malformed, so the
check can run in CI.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ADR = ROOT / "docs" / "adr"
README = ADR / "README.md"
AREAS = ["hardware", "ota-release", "radio", "ux", "display", "platform"]
STATUSES = ("Accepted", "Open", "Superseded", "Dropped")


def parse(path: Path) -> dict:
    text = path.read_text(encoding="utf-8")
    head = text[:2000]
    m = re.match(r"# (.+)", head)
    if not m:
        raise ValueError(f"{path.name}: no '# Title' line")
    rec = {"file": path.name, "title": m.group(1).strip()}
    for key, pat in (
        ("status", r"\*\*Status:\*\*\s*([^\n]+)"),
        ("area", r"\*\*Area:\*\*\s*([a-z-]+)"),
        ("first", r"\*\*First decided:\*\*\s*(\d{4}-\d{2}-\d{2})"),
        ("last", r"\*\*Last changed:\*\*\s*(\d{4}-\d{2}-\d{2})"),
    ):
        mm = re.search(pat, head)
        if not mm:
            raise ValueError(f"{path.name}: header lacks {key}")
        rec[key] = mm.group(1).strip()
    if rec["area"] not in AREAS:
        raise ValueError(f"{path.name}: area '{rec['area']}' not in {AREAS}")
    if not rec["status"].startswith(STATUSES):
        raise ValueError(f"{path.name}: status '{rec['status']}' not one of {STATUSES}")
    if not path.name.startswith(rec["first"]):
        raise ValueError(f"{path.name}: file date != First decided {rec['first']}")
    return rec


def status_short(s: str) -> str:
    """'Accepted (one thing open ...)' -> 'Accepted'; the record holds the detail."""
    return re.split(r"\s+by\s+|\s*\(|;|,", s, 1)[0].strip()


def cell(s: str) -> str:
    return s.replace("|", "\\|")


def main() -> int:
    recs = []
    errors = []
    for p in sorted(ADR.glob("*.md")):
        if p.name == "README.md":
            continue
        try:
            recs.append(parse(p))
        except ValueError as e:
            errors.append(str(e))
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1

    out = ["### By area", ""]
    for area in AREAS:
        rows = [r for r in recs if r["area"] == area]
        if not rows:
            continue
        out.append(f"#### {area}")
        out.append("")
        for r in sorted(rows, key=lambda r: (r["first"], r["file"])):
            out.append(f"- {r['first']} [{cell(r['title'])}]({r['file']}) "
                       f"({status_short(r['status'])}; last changed {r['last']})")
        out.append("")
    out += ["### By date", "", "| First decided | Last changed | Record | Area | Status |",
            "|---|---|---|---|---|"]
    for r in sorted(recs, key=lambda r: (r["first"], r["file"])):
        out.append(f"| {r['first']} | {r['last']} | [{cell(r['title'])}]({r['file']}) | "
                   f"{r['area']} | {status_short(r['status'])} |")

    text = README.read_text(encoding="utf-8")
    start, end = "<!-- index:start -->", "<!-- index:end -->"
    i, j = text.find(start), text.find(end)
    if i < 0 or j < 0 or j < i:
        print("README.md lacks the index:start / index:end markers", file=sys.stderr)
        return 1
    text = text[:i + len(start)] + "\n" + "\n".join(out) + "\n" + text[j:]
    README.write_text(text, encoding="utf-8")
    print(f"indexed {len(recs)} records")
    return 0


if __name__ == "__main__":
    sys.exit(main())
