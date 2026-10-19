#!/usr/bin/env python3
"""Build a release's meta.json and hold the flash-epoch line (release workflow).

    release_meta.py <tag> <layout-dir> <old-site-dir> <out-dir>

<layout-dir> holds one `<slug>.json` per board from `tools/flash_layout.py
describe` (the build job writes them). <old-site-dir> is the gh-pages checkout
(only its */meta.json are read; may be missing on a first deploy). Writes:

  <out-dir>/meta.json     {"epoch": {slug: N}, "layout": {slug: {...}}}
                          -> served at <site>/<tag>/meta.json. The device
                          reads ONLY the epoch map, with a small fetch, so it
                          comes first and must fit kMaxMetaBytes
                          (update_check.cpp) — enforced below.
  <out-dir>/webflash.md   the "WEB FLASH REQUIRED" line, or empty. Prepended
                          to the release notes and notes.txt ONLY when this
                          release raises some board's epoch: firmware from
                          before the levers can't read meta.json, so the
                          notes are all it has to explain a failed install.

THE RULE, per slug, against the newest older release that has a meta.json:
  bootloader or partition table changed, epoch NOT raised  -> FAIL the run
  epoch lowered                                            -> FAIL the run
  epoch raised, nothing under the app changed              -> warning
A board new to the matrix, or no earlier meta.json at all, is a baseline.
"""

import json
import os
import re
import sys

STABLE = re.compile(r"v\d+\.\d+\.\d+$")
BETA = re.compile(r"v\d+\.\d+\.\d+-beta\.\d+$")
DEVICE_META_BUDGET = 448  # < core::kMaxMetaBytes (512), include/core/release_meta.h

# What the notes call each slug. Unlisted slugs (a new board) print as-is.
PRODUCT = {
    "p4-wifi6-touch-lcd-5": "P4-WIFI6-Touch-LCD-5",
    "p4-wifi6-touch-lcd-4.3": "P4-WIFI6-Touch-LCD-4.3",
    "p4-wifi6-touch-lcd-x-8": 'P4-WIFI6-Touch-LCD-X 8"',
    "s3-touch-lcd-4.3c": "S3-Touch-LCD-4.3C",
}


def order_key(v):
    # Same ordering as order_key() in the workflow's pages job and the
    # device's semver_newer(): a release outranks every beta of its number.
    core, _, pre = v[1:].partition("-beta.")
    return [int(x) for x in core.split(".")] + ([0, int(pre)] if pre else [1, 0])


def previous_meta(tag, old_dir):
    if not os.path.isdir(old_dir):
        return None, None
    older = []
    for d in os.listdir(old_dir):
        if not (STABLE.fullmatch(d) or BETA.fullmatch(d)) or d == tag:
            continue
        if order_key(d) < order_key(tag) and os.path.isfile(os.path.join(old_dir, d, "meta.json")):
            older.append(d)
    if not older:
        return None, None
    prev = max(older, key=order_key)
    return prev, json.load(open(os.path.join(old_dir, prev, "meta.json")))


def under_app(layout):
    return (layout["bootloader"]["sha256"], layout["partitions_sha256"])


def main(argv):
    if len(argv) != 5:
        print(__doc__, file=sys.stderr)
        return 2
    tag, layout_dir, old_dir, out_dir = argv[1:]
    layouts = {}
    for name in sorted(os.listdir(layout_dir)):
        if name.endswith(".json"):
            layouts[name[:-len(".json")]] = json.load(open(os.path.join(layout_dir, name)))
    if not layouts:
        print("::error::no board layouts in %s" % layout_dir)
        return 1

    meta = {"epoch": {s: l["epoch"] for s, l in layouts.items()},
            "layout": layouts}
    text = json.dumps(meta, indent=1)
    head = text.index('"layout"')
    if head > DEVICE_META_BUDGET:
        print("::error::meta.json's epoch map is %d bytes; devices read only the first "
              "%d (kMaxMetaBytes). Raise both together." % (head, DEVICE_META_BUDGET))
        return 1

    prev, old = previous_meta(tag, old_dir)
    errors, bumped = [], []
    if old is None:
        print("::notice::no earlier meta.json on the site: %s is the flash-epoch baseline" % tag)
    else:
        for slug, lay in layouts.items():
            if slug not in old.get("layout", {}):
                print("::notice::%s is new since %s: baseline epoch %d" % (slug, prev, lay["epoch"]))
                continue
            was, now = old["epoch"][slug], lay["epoch"]
            changed = under_app(old["layout"][slug]) != under_app(lay)
            if now < was:
                errors.append("%s: flash epoch went DOWN (%d in %s, %d now)" % (slug, was, prev, now))
            elif changed and now == was:
                errors.append(
                    "%s: the bootloader or partition table changed since %s but its flash "
                    "epoch is still %d. OTA can't install that: bump custom_flash_epoch "
                    "for the env (a web-flash release), or restore the pinned files. "
                    "bootloader %s -> %s (IDF %s -> %s), partitions %s -> %s"
                    % (slug, prev, now,
                       old["layout"][slug]["bootloader"]["sha256"][:12], lay["bootloader"]["sha256"][:12],
                       old["layout"][slug]["bootloader"]["idf"], lay["bootloader"]["idf"],
                       old["layout"][slug]["partitions_sha256"][:12], lay["partitions_sha256"][:12]))
            elif now > was:
                bumped.append(slug)
                if not changed:
                    print("::warning::%s: flash epoch raised %d -> %d but nothing under the app "
                          "changed since %s; units will be told to web flash for nothing"
                          % (slug, was, now, prev))
    for e in errors:
        print("::error::" + e)
    if errors:
        return 1

    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "meta.json"), "w") as f:
        f.write(text + "\n")
    line = ""
    if bumped:
        boards = []
        for s in bumped:
            name = PRODUCT.get(s[:-len("-rev3")] if s.endswith("-rev3") else s, s)
            if name not in boards:
                boards.append(name)
        line = ("**WEB FLASH REQUIRED** (%s): this update can't be installed over WiFi "
                "on those boards. Install it from https://mlsorensen.github.io/Apollo2/ "
                "with a USB cable - settings are kept. An over-the-air install will fail "
                "and Apollo will go back to the version it had.\n" % ", ".join(boards))
        print("::notice::flash epoch raised for %s - notes get the web-flash line" % ", ".join(bumped))
    with open(os.path.join(out_dir, "webflash.md"), "w") as f:
        f.write(line)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
