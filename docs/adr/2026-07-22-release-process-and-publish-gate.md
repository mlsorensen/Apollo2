# Releases are built by CI and published only after a maintainer approves

- **Status:** Accepted
- **Area:** ota-release
- **First decided:** 2026-07-22 · **Last changed:** 2026-10-23
- **Boards:** all released boards

## Context

Every fielded device self-updates from the GitHub Pages site, so whoever can
run the release workflow ships what every machine installs. An OTA security
review (2026-09-19) found the transport sound (cert bundle,
`VERIFY_REQUIRED`, hostname check, TLS 1.2) and the real trust boundary to be
GitHub itself. Separately, releases had gone out wrong by hand: v0.8.0
(2026-08-24) and v0.12.3 (2026-09-10) reported the previous version because
`fw::kVersion` was not bumped, so the update check re-prompted after every
reboot; v0.12.2 shipped work whose notes sat under a later version's heading.

## Decision

- CI builds every release env and publishes; nothing is built or uploaded
  locally. The only entry point is `gh workflow run firmware-release.yml -f
  tag=vX.Y.Z`; CI creates the tag and the Release.
- The workflow token is read-only. The `release` and `pages` jobs run in the
  `release` environment (required reviewer: a maintainer), which alone holds the
  write credential, a fine-grained PAT (`RELEASE_TOKEN`, this repo, Contents
  read+write). Rulesets let only an admin push `gh-pages` or create a `v*`
  tag, so a workflow edit re-granting `contents: write` still cannot
  publish. Publishing actions are SHA-pinned. One approval releases both
  jobs. Betas are gated too: beta devices install straight from
  `releases-beta.json`.
- Stable tags are cut from `main` only (a guard step fails any other ref).
  Betas may be cut from a branch with `--ref`; the tag lands on the commit
  that was built, never on main's head.
- Promotion is a REBUILD at the real tag, never a copy of the beta's
  artifacts: `kVersion` is compiled in.
- `kVersion == tag` and `kUpdateSlug == matrix board` are proven at COMPILE
  time: the workflow passes `APOLLO_EXPECT_VERSION` / `APOLLO_EXPECT_SLUG` as
  unquoted defines and `include/build_check.h` stringises them for
  `static_assert`s in `version.h` and `board_config.h`.
- Notes come from `CHANGELOG.md`'s `## vX.Y.Z` section (features, then
  fixes; bullets under ~50 words); unreleased work sits under `## Unreleased`
  and the release commit renames it. A beta falls back to its `## vX.Y.Z`
  section.

## Evidence

| Date | Event |
|---|---|
| 2026-09-21 | v0.14.2, first gated release: one approval released both jobs. The PAT-created tag fired the `push: tags` trigger and spawned a duplicate waiting run; trigger removed (`0122b0b`) |
| 2026-09-23 | v1.0.0-beta.1 to 3 had tags on main's head, not the built commit; pinned to `github.sha`, tags moved (`c8c6779`) |
| 2026-10-23 | v1.1.0's S3 build failed the grep-the-image version check although `kVersion` was right: the xtensa linker tail-merged "1.1.0" into the TZ table's "...,M11.1.0". Replaced by the compile-time assert (`605e553`) |

## Alternatives and dead ends

- CI-side image signing, rejected 2026-09-19: a key in a repository secret
  is readable by the same people who can run the workflow, so it moves no
  trust boundary, only adds CDN and certificate failure cases and creates a
  key that can never be lost.
- Grepping the finished image for the NUL-delimited version and slug
  (2026-09-10 to 2026-10-23): works only while the literal survives the
  linker; it did not. An interim `"apollo-fw=<version>"` marker (`8f6c1d6`,
  2026-10-23 19:58) lived seven minutes before the compile-time assert
  replaced it (`605e553`, 20:05).

## Consequences

- The PAT expires and must be rotated; a 403 on the tag or `gh-pages` push
  means a missing or expired PAT, not a code bug.
- The symbols upload can time out and leave the Release a draft with no tag
  while OTA is already live from Pages (v0.14.1, 2026-09-17): upload the
  tarball by hand and un-draft; do not re-run.
- Never rebase or squash a branch after tagging a beta on it; merge main
  into it.

## History

- 2026-07-22: release workflow and web flasher (`bf203de`); notes from
  CHANGELOG.md since v0.4.0 (`93a925d`, 2026-07-26).
- 2026-09-10: v0.12.3 re-cut; CI asserts version == tag (`70cc037`).
- 2026-09-19: approval gate, read-only token, rulesets (`4cc216f`).
- 2026-09-22: stable from main only, betas from a branch (`a7ae510`).
- 2026-10-23: compile-time asserts replace the image grep (`605e553`).

## Sources

`bf203de`, `93a925d`, `70cc037`, `4cc216f`, `0122b0b`, `a7ae510`, `c8c6779`,
`8f6c1d6`, `605e553`; `.github/workflows/firmware-release.yml`,
`include/build_check.h`, `include/version.h`, `CHANGELOG.md`.
