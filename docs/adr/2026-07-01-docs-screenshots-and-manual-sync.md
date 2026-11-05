# Docs rules: 800x480 is the reference size, screenshots come from the sim, the manual moves with the code

- **Status:** Accepted
- **Area:** ux
- **First decided:** 2026-07-01 · **Last changed:** 2026-08-31
- **Boards:** all (the rule is about the documentation)

## Context

The UI compiles unchanged in a host simulator that renders PNGs
(`2026-06-28-host-simulator-ui-verification.md`), so every screen can be
pictured without a board. GitHub cannot show the git-ignored `renders/`,
and a manual that describes screens by hand drifts from them. The standing
rule is that docs never drift from the firmware.

## Decision

- **800x480 is the reference size.** Every documentation screenshot and
  example render is the 800x480 layout (the 4.3" panels, and the logical
  size the 5" and 4.3" P4 boards scale from). Other sizes appear only when
  the point is to show that the UI scales
  ([every manifest entry is 800x480](#evidence)).
- **Screenshots are generated, never captured**
  ([why](#alternatives-and-dead-ends)). README images are curated copies of
  sim renders in `docs/img/`; the manual's come from `make docs-img`
  (`tools/annotate_docs.py` draws numbered callout boxes from
  `docs/img/manual/manifest.json` onto the deterministic renders).
  Refreshing them is part of any UI change ([the rule at work](#evidence)).
- **MANUAL.md is updated in the same change** as any feature, setting,
  screen or board change: the prose and defaults, the "Where everything
  lives" tree, the TOCs, `docs/HARDWARE.md` for boards, and the screenshots.
  When a page's layout moves, the manifest's callout boxes are re-measured;
  the script warns when a render's size changes, but same-size row shifts
  must be eyeballed.
- **User-facing docs cover released boards only.** An internal-only board's
  change updates CLAUDE.md and the code, never README, HARDWARE or the site.

## Consequences

- The DOCS SYNC RULE in CLAUDE.md binds every change; the README's
  simulator section states the screenshot rule for contributors.
- `docs/img/manual/manifest.json` is a maintained artifact: a layout change
  without a manifest check ships a callout pointing at the wrong row.

---

## Details

### Evidence

- All 38 entries of the manual's manifest are 800x480.
- 2026-08-31 docs overhaul (`867dd37`): a TOC, a navigation map of every
  screen and setting, a screenshot for every major section with callouts
  referenced from the bullets; two sim renders added for coverage.
- The legend work of 2026-11-04 regenerated the manual images three times in
  one day (`a82f7c7`, `c6fca82`, `62218cc`) and refit the flow-graph callout
  each time, which is the rule working as intended.

### Alternatives and dead ends

- A mermaid rendering of the navigation map (`867dd37`): dropped the same
  day for the ASCII tree, "too big" (`f3594d1`).
- Callouts in the UI's accent blue: ambiguous next to real blue widgets;
  recoloured to a magenta the UI never uses (`f3594d1`).
- Hand-captured screenshots from a board: never used; they cannot be
  regenerated and the renders are deterministic.

### History

- 2026-06-28: the first 800x480 render (`bbe52e3`).
- 2026-07-01: README screenshots from `docs/img/` with a keep-in-sync note
  (`f69d5a3`); 800x480 made the reference size (`ed9781c`, "use 800x480
  screenshots as the reference size"), the day the README gallery was
  built.
- 2026-07-24: manual sync becomes a standing instruction (docs landed with
  `ded5d27`).
- 2026-08-31: the rule widened to the manual's callouts, map and TOCs,
  `make docs-img`; HARDWARE.md split out (`24a02d3`, `867dd37`, `f3594d1`).

### Sources

Commits `bbe52e3`, `f69d5a3`, `ed9781c`, `ded5d27`, `24a02d3`, `867dd37`,
`f3594d1`;
`tools/annotate_docs.py`; `docs/img/manual/manifest.json`; CLAUDE.md "DOCS
SYNC RULE"; README.md "Screenshots".
