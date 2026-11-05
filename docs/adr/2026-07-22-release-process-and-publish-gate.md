# Releases are built by CI and published only after a maintainer approves

- **Status:** Accepted
- **Area:** ota-release
- **First decided:** 2026-07-22 · **Last changed:** 2026-10-23
- **Boards:** all released boards

## Context

Every fielded device self-updates from the GitHub Pages site, so whoever can
run the release workflow ships what every machine installs; the OTA transport
is sound and the trust boundary is GitHub itself
([the review](#ev-hand-releases)). Releases cut by hand have gone out wrong
more than once, usually an unbumped `fw::kVersion` that re-prompts the update
check after every reboot ([which releases](#ev-hand-releases)).

## Decision

- CI builds and publishes every release env; nothing is built or uploaded
  locally. The one entry point is `gh workflow run firmware-release.yml -f
  tag=vX.Y.Z`; CI creates the tag and the Release.
- The workflow token is read-only. The `release` and `pages` jobs wait in the
  `release` environment for one maintainer approval; only it holds the write
  credential, a fine-grained PAT (`RELEASE_TOKEN`, this repo, Contents
  read+write), and rulesets let only an admin push `gh-pages` or create a
  `v*` tag, so a workflow edit re-granting `contents: write` still cannot
  publish. Publishing actions are SHA-pinned. Betas are gated too; beta
  devices install straight from `releases-beta.json`
  ([why not image signing](#alternatives-and-dead-ends)).
- Stable tags are cut from `main` only (a guard step). Betas may be cut from
  a branch with `--ref`; the tag lands on the built commit
  ([why](#ev-gate-events)).
- Promotion is a rebuild at the real tag, never a copy: `kVersion` is
  compiled in.
- `kVersion == tag` and `kUpdateSlug == matrix board` are compile-time
  `static_assert`s against `APOLLO_EXPECT_VERSION` / `APOLLO_EXPECT_SLUG`
  (`include/build_check.h`; [why not a grep of the
  image](#alternatives-and-dead-ends)).
- Notes come from `CHANGELOG.md`'s `## vX.Y.Z` section (features, then
  fixes; bullets under ~50 words); unreleased work sits under `## Unreleased`
  until the release commit renames it. A beta falls back to its `## vX.Y.Z`
  section.

## Consequences

- The PAT expires and must be rotated; a 403 on the tag or `gh-pages` push
  is a missing or expired PAT, not a code bug.
- A timed-out symbols upload leaves the Release a draft with no tag while
  OTA is already live from Pages: upload the tarball by hand and un-draft,
  do not re-run ([seen once](#ev-hand-releases)).
- Never rebase or squash a branch after tagging a beta on it; merge main
  into it.

---

## Details

### Evidence

<a id="ev-hand-releases"></a>
Releases that went wrong before or outside the gate, and the review that
shaped it:

| Date | Release | What happened |
|---|---|---|
| 2026-08-24 | v0.8.0 | reported the previous version: `fw::kVersion` not bumped, so the update check re-prompted after every reboot |
| 2026-09-10 | v0.12.3 | the same unbumped `fw::kVersion`; re-cut the same day |
| 2026-09-10 | v0.12.2 | shipped work whose notes sat under a later version's heading |
| 2026-09-17 | v0.14.1 | the symbols upload timed out and left the Release a draft with no tag while OTA was already live from Pages; repaired by hand |
| 2026-09-19 | review | OTA security review: transport sound (cert bundle, `VERIFY_REQUIRED`, hostname check, TLS 1.2); the real trust boundary is GitHub itself |

<a id="ev-gate-events"></a>
Events under the gate:

| Date | Event |
|---|---|
| 2026-09-21 | v0.14.2, first gated release: one approval released both jobs. The PAT-created tag fired the `push: tags` trigger and spawned a duplicate waiting run; trigger removed (`0122b0b`) |
| 2026-09-23 | v1.0.0-beta.1 to 3 had tags on main's head, not the built commit; pinned to `github.sha`, tags moved (`c8c6779`) |
| 2026-10-23 | v1.1.0's S3 build failed the grep-the-image version check although `kVersion` was right: the xtensa linker tail-merged "1.1.0" into the TZ table's "...,M11.1.0". Replaced by the compile-time assert (`605e553`) |

### Alternatives and dead ends

- CI-side image signing, rejected 2026-09-19: a key in a repository secret
  is readable by the same people who can run the workflow, so it moves no
  trust boundary, only adds CDN and certificate failure cases and creates a
  key that can never be lost.
- Grepping the finished image for the NUL-delimited version and slug
  (2026-09-10 to 2026-10-23): works only while the literal survives the
  linker; it did not. An interim `"apollo-fw=<version>"` marker (`8f6c1d6`,
  2026-10-23 19:58) lived seven minutes before the compile-time assert
  replaced it (`605e553`, 20:05): the workflow passes the expected version
  and slug as unquoted defines, `include/build_check.h` stringises them, and
  `static_assert`s in `version.h` and `board_config.h` compare.

### History

- 2026-07-22: release workflow and web flasher (`bf203de`); notes from
  CHANGELOG.md since v0.4.0 (`93a925d`, 2026-07-26).
- 2026-09-10: v0.12.3 re-cut; CI asserts version == tag (`70cc037`).
- 2026-09-19: approval gate, read-only token, rulesets (`4cc216f`), after
  the OTA security review of the same day.
- 2026-09-22: stable from main only, betas from a branch (`a7ae510`).
- 2026-10-23: compile-time asserts replace the image grep (`605e553`).

### Sources

`bf203de`, `93a925d`, `70cc037`, `4cc216f`, `0122b0b`, `a7ae510`, `c8c6779`,
`8f6c1d6`, `605e553`; `.github/workflows/firmware-release.yml`,
`include/build_check.h`, `include/version.h`, `CHANGELOG.md`.
