# One env while iterating, never build while flashing, the full matrix once as a single invocation

- **Status:** Accepted
- **Area:** platform
- **First decided:** 2026-07-25 · **Last changed:** 2026-10-30
- **Boards:** all envs

## Context

PlatformIO envs share `.pio/build/<env>` and one package cache, and
development happens at the bench, flashing the board under test from the
same checkout: a background build of that env clobbers object files
mid-link and yields linker errors that look like code bugs ([what that
looks like](#ev-corruption)). A full matrix mid-debug costs minutes per
change and tells nothing one env would not. Unpinned platform and library
versions let two envs silently build different code ([seen
twice](#ev-pins)).

## Decision

- While iterating, build one env: the board being tested, plus the host sim
  for UI work (`make sim` has its own `.pio/build/sim`, needs no node, and
  renders every screen).
- Never build a device env while the board may be being flashed; if in
  doubt, let the bench's own `make flash-*` build it.
- Run the full matrix once, at the end, as one multi-env invocation (`pio
  run -e a -e b ...`); parallel processes share the package cache and are
  unsafe. `make build-all` must pass before a push or release of a platform
  change, not per edit ([why not per commit](#alternatives-and-dead-ends)).
- The platform is pinned to an explicit release URL (55.03.312-1, [since
  2026-10-18](#history)) and every library to an exact version (lvgl 9.5.0,
  GFX 1.6.6, NimBLE-Arduino 2.5.0, esp-nimble-cpp 2.5.0, ArduinoJson 7.4.3)
  ([why exact](#ev-pins)).
- Release images are CI's job ([since](#history)); nothing is built locally
  for a release unless a platform, bootloader or partition-table change
  needs a bench unit for the OTA proof.

## Consequences

- `webapp_dist.h` is generated and git-ignored; bare `pio run` does not
  build it (`make webapp` first).
- A library bump is deliberate, one at a time, with a bench check.
- Device-only code (links, provisioner, history, io_extension) is not in
  the sim and still needs one careful device compile.
- A change to `PLATFORMIO_BUILD_FLAGS` wipes every env's build directory
  ([measured](#ev-checksum)); never run one while another env's
  `firmware.bin` is still wanted.
- The platform needs PlatformIO Core >= 6.2.0 on Python <= 3.14
  ([detail](#ev-python)).

---

## Details

### Evidence

<a id="ev-corruption"></a>
- A background build of the env being flashed fails the link with
  "cannot find ... ble_hw.c.o"; recovery is a `-t clean` and a full
  ESP/NimBLE recompile. A header included everywhere (core/system.h) makes
  every env a ~2.5 min recompile.
- 2026-07-25 and 2026-07-30: the rule was restated three times after
  matrix builds were run mid-debug; the third time the full matrix ran during
  a live hardware session.

<a id="ev-pins"></a>
- 2026-07-17: the `stable` platform label had resolved to 55.03.39 but does
  not force a reinstall, so builder and framework versions could silently
  mix; pinned explicitly.
- 2026-10-17: with `^` ranges the P4-4.3 rev3 env had LVGL 9.6.0 + GFX 1.6.8
  while the S3 had 9.5.0 + 1.6.6, and a rebuilt ELF no longer matched the
  flashed one for a coredump decode. LVGL 9.6 was A/B'd on the 4.3C first
  (identical frame-time histograms, no memory gain) and not taken.

<a id="ev-checksum"></a>
- 2026-10-23: `PLATFORMIO_BUILD_FLAGS` is folded into
  `.pio/build/project.checksum`; a changed checksum deletes the whole build
  directory, every env. The release workflow's `-DAPOLLO_EXPECT_VERSION`
  defines and the `APOLLO_SKIP_C6_UPDATE` recipe both do this. Harmless in
  CI (one env per runner); on the bench expect full rebuilds and never run it
  while another env's `firmware.bin` is still wanted.

<a id="ev-python"></a>
- 2026-10-18: platform 55.03.312-1 needs PlatformIO Core >= 6.2.0 on
  Python <= 3.14; a Homebrew install on Python 3.15 refuses it (CI uses
  3.11).

### Alternatives and dead ends

- A per-commit matrix during development: rejected as the problem itself.
- A grep of the finished image for the version string as the release gate:
  the xtensa linker tail-merged "1.1.0" into a TZ string and refused a correct
  build (2026-10-23); replaced by compile-time static_asserts.
- `make build-release` as a release step: dropped on 2026-10-30, CI builds
  every release env before the approval gate.

### History

- 2026-07-17: platform pinned explicitly (`a84f57b`).
- 2026-07-25 and 2026-07-30: one-env rule set and reinforced (recorded in
  the working notes).
- 2026-09-09: CLAUDE.md build rule amended: full sweeps before push or
  release, not per edit (`7760dfe`).
- 2026-10-17: exact library pins (`9840933`).
- 2026-10-18: platform 55.03.312-1 (`99f66fd`).
- 2026-10-23: build-flag checksum wipe noted; static_assert release gate
  (`605e553`).
- 2026-10-30: `make build-release` removed from the release checklist
  (`cec4258`); the Makefile target stays for platform-change proofs.

### Sources

Commits `a84f57b`, `7760dfe`, `9840933`, `99f66fd`, `605e553`, `cec4258`;
`platformio.ini`; `Makefile` (sweeps); CLAUDE.md "Boards / build" and "Release checklist";
the global notes' "Builds" section.
