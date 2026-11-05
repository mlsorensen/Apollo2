# NVS settings are add-only, and a restore from the card is a replace

- **Status:** Accepted
- **Area:** ota-release
- **First decided:** 2026-09-07 · **Last changed:** 2026-11-04
- **Boards:** all; the backup page exists only on boards with a card slot

## Context

All settings live in one Preferences namespace, `micra`, in the 20 KB `nvs`
partition. Every getter is `isKey()`-guarded with a compiled default, so a
key an older build never heard of is inert. The unsafe direction is an OLD
build reading a WIDENED value: stored enums index fixed arrays (the update
cadence into `kCadence[]`, for one), so a value past an older build's array
is an out-of-bounds read in a binary that can no longer be patched. And "old
firmware reads new NVS" is not only a user's downgrade: `verifyRollbackLater()`
plus the 60 s healthy-loop confirm reverts a bad image UNATTENDED, and betas
make that routine.

Separately, settings do not travel with the SD card the shot history is on:
swap the hardware and every shot survives while every preference is lost.

## Decision

The NVS rule: a release may only ADD keys to `micra`, never change the
meaning, type or value range of a key an older shipped build reads. New
semantics = a new key, the old one read as a fallback. Old keys are never
deleted. Clamp on read wherever a stored value indexes an array.

| Old key | New key | Why |
|---|---|---|
| `updchk` (bool) | `updmode` (int), 2026-09-07 | on/off became Off / On boot / Daily |
| `ssstyle` (0 Logo, 1 Blank) | `ssstyle2` (0 Logo, 1 Dim, 2 Off), 2026-09-10 | Dim at index 1 would have flipped every existing user |
| `rdychimev` (linear %) | `rdychimeu` (perceptual), 2026-09-11 | migrated via `migrate_chime_volume` |
| (new) | `updbeta`, 2026-09-12 | a widened `updmode` would be indexed by older builds |

Settings backup (`Settings > Apollo > Backup`, 2026-09-12): the device
`ShotStore` implements `core::ISettingsBackup` as a second port, two more job
kinds on the writer task that owns the mount; no second mount, task or
mutex. `config_backup.cpp` ENUMERATES NVS rather than listing keys, with
type-tagged lines (`i32:`, `str:`, `blob:` hex for floats), so values
round-trip exactly and a later setting is carried automatically.

RESTORE IS A REPLACE: hold the per-unit keys, `nvs_erase_all`, apply the
file, reboot. A key the file lacks reads its compiled default, so an older
backup lands exactly like an upgrade and the fallbacks above do the rest. A
backup from NEWER firmware is refused via `core::semver_newer` (shared with
the update check), for the widened-enum reason.

Per-unit keys never leave the board: `padsense`, `paddrive`, `padwire`,
`otainst`, `sprpend` / `sprpend2`, `lastunix` and `_init` are kept out of the
file (`is_unit_key`). Of those, `padsense`, `paddrive`, `padwire`, `lastunix`
and `otainst` are read out before the erase and written back after it
(`UnitKeys`); `sprpend` / `sprpend2` are one-boot flags dropped on purpose
and `_init` is re-created at the next boot. (`padwire` joined the list on
2026-11-03 without the hold, so a restore on v1.3.0-beta.1 to beta.3 reset
the wiring choice to Auto; fixed 2026-11-04 for v1.3.0.) The credentials are OPT-OUT switches on the confirm
modal, the file being plain text on a card that leaves the machine: the WiFi
group (`ssid`,
`wifipass`, `wifi_en`, plus the static-address keys since v1.0.0-beta.7) and
`token`. `mac` and `name` always travel: they identify the machine, they do
not open it.

## Evidence

The hazard is structural; no unit has had to be recovered from it. Nearest
miss: v0.12.1 (2026-09-10, `8d0962b`) widened `updmode` to 3 (Hourly) two
days before the rule was written. v0.12.0's two `kCadence[]` sites computed
the index modulo the array (the cycle button) and clamped it (the Settings
row, `(m >= 0 && m <= 2) ? m : 1`), so a rolled-back v0.12.0 reading a 3
showed "On boot" rather than reading past the end.

## Alternatives and dead ends

- Restore as a MERGE: rejected; keys the file does not carry would survive,
  so the device after a restore would not be the one the backup described.
- A hand-maintained key list in the backup code: rejected, it drifts.
- Deleting superseded keys: never; a rolled-back build still reads them.

## Consequences

- Every new setting is a new key with a default and a guard; widening an
  existing enum is a bug even where the current build clamps.
- The Backup page itself (notices, the confirm modal, the Storage page) is
  the ux record `2026-09-12-backup-and-storage-ux.md`; this record owns the
  compatibility rules it rests on.
- Backup and restore run outside the scale-roles persist lock (restore
  reboots; accepted 2026-10-24).

## History

- 2026-09-07: first fallback pair, `updchk` to `updmode` (`9140cf2`).
- 2026-09-10: `ssstyle2` (`8d0962b`); 2026-09-11 — `rdychimeu` (`4c2beef`).
- 2026-09-12: rule stated in the beta-channel change (`4379227`); backup
  and restore shipped (`6305121`, `8cea6ad`) in v0.14.0.
- 2026-09-30: static-IP keys join the WiFi opt-out group (`c28f459`,
  v1.0.0-beta.7).
- 2026-10-24: bench-scale keys added as the `2`-suffixed set (`0134038`).
- 2026-11-03: `padwire` added to the keys kept out of the file (`455e7c9`)
  but not to the set held across the erase.
- 2026-11-04: `padwire` added to `UnitKeys`, so a restore keeps it (found by
  the decision-log audit; issue #8).

## Sources

`9140cf2`, `8d0962b`, `4c2beef`, `4379227`, `6305121`, `8cea6ad`, `c28f459`,
`0134038`, `455e7c9`;
`src/platform_esp32/config.cpp`, `src/platform_esp32/config_backup.cpp`,
`include/core/semver.h`.
