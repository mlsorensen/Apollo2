#!/usr/bin/env python3
"""Generate the user manual's annotated screenshots.

Reads docs/img/manual/manifest.json, draws numbered callout boxes onto
simulator renders (renders/*.png, produced by `make sim`), and writes the
results to docs/img/manual/*.png. Run via `make docs-img`.

The renders are bit-deterministic, so the manifest's pixel coordinates stay
valid until the UI actually changes. When it does: re-run `make sim`, check
the affected screenshots, and adjust the manifest's boxes to match. A size
mismatch between a render and the manifest's expected size is reported
loudly — it means the UI moved and every box on that image needs re-checking.
"""

import json
import sys
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    sys.exit("annotate_docs: needs Pillow (pip install pillow)")

REPO = Path(__file__).resolve().parent.parent
MANIFEST = REPO / "docs/img/manual/manifest.json"
OUT_DIR = REPO / "docs/img/manual"

ACCENT = (47, 155, 244, 255)   # callout box + badge fill (theme accent blue)
BADGE_TEXT = (255, 255, 255, 255)
BOX_W = 3                       # box outline width
BADGE_D = 30                    # badge diameter


def load_font(size):
    for path in (
        "/System/Library/Fonts/Helvetica.ttc",     # macOS
        "/Library/Fonts/Arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",  # linux
    ):
        try:
            return ImageFont.truetype(path, size)
        except OSError:
            continue
    return ImageFont.load_default()


def annotate(entry, font):
    src = REPO / entry["src"]
    out = OUT_DIR / entry["out"]
    if not src.exists():
        print(f"  MISSING render {entry['src']} — run `make sim` first", file=sys.stderr)
        return False
    im = Image.open(src).convert("RGBA")
    ew, eh = entry.get("size", [im.width, im.height])
    if (im.width, im.height) != (ew, eh):
        print(f"  SIZE CHANGED {entry['src']}: {im.width}x{im.height} != {ew}x{eh}"
              f" — the UI moved; re-check every box for {entry['out']}", file=sys.stderr)
        return False
    layer = Image.new("RGBA", im.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    for c in entry.get("callouts", []):
        x, y, w, h = c["x"], c["y"], c["w"], c["h"]
        d.rounded_rectangle([x, y, x + w, y + h], radius=8, outline=ACCENT, width=BOX_W)
        # Numbered badge centered on the box's top-left corner (clamped inside).
        bx = max(x - BADGE_D // 2, 1)
        by = max(y - BADGE_D // 2, 1)
        d.ellipse([bx, by, bx + BADGE_D, by + BADGE_D], fill=ACCENT)
        label = str(c["n"])
        tw = d.textlength(label, font=font)
        d.text((bx + (BADGE_D - tw) / 2, by + 4), label, font=font, fill=BADGE_TEXT)
    im = Image.alpha_composite(im, layer).convert("RGB")
    out.parent.mkdir(parents=True, exist_ok=True)
    im.save(out)
    print(f"  wrote {out.relative_to(REPO)}")
    return True


def main():
    entries = json.loads(MANIFEST.read_text())
    font = load_font(18)
    ok = True
    for entry in entries:
        ok &= annotate(entry, font)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
