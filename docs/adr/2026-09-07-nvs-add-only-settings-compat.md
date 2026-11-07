# NVS settings are add-only, and a restore from the card is a replace

- **Status:** Accepted
- **Area:** ota-release
- **First decided:** 2026-09-07 · **Last changed:** 2026-11-07
- **Boards:** all; the backup page exists only on boards with a card slot

## Context

All settings live in one Preferences namespace, `micra`, in the 20 KB `nvs`
partition, every getter `isKey()`-guarded with a compiled default, so an
unknown key is inert. The unsafe direction is an OLD build reading a WIDENED
value: stored enums index fixed arrays (`kCadence[]`, for one), so a value
past the array is an out-of-bounds read in a binary nobody can patch, and OTA
rollback puts an older build back UNATTENDED ([the nearest
miss](#ev-near-miss)). Separately, settings do not travel with the SD card
the shot history is on.

## Decision

The NVS rule: a release may only ADD keys to `micra`, never change the
meaning, type or value range of a key an older shipped build reads. New
semantics = a new key, the old one read as a fallback
([the pairs so far](#ev-fallbacks)). Old keys are never deleted. Clamp on
read wherever a stored value indexes an array.

Settings backup (`Settings > Apollo > Backup`): the device `ShotStore`
implements `core::ISettingsBackup` as a second port, two more job kinds
(`JobKind::kBackup` / `kRestore`) on the writer task that owns the mount; no
second mount, task or mutex. The UI sees only the port; a board without a
card slot (no `BOARD_HAS_SD_MMC`) passes `nullptr` and has no Backup page.
`config_backup.cpp` ENUMERATES NVS ([why not a key
list](#alternatives-and-dead-ends)) into type-tagged lines (`i32:`, `str:`,
`blob:` hex for floats), so values round-trip exactly.

RESTORE IS A REPLACE ([why not a merge](#alternatives-and-dead-ends)): hold
the per-unit keys, `nvs_erase_all`, apply the file, reboot through
`App::set_reboot_handler` (a full `esp_restart`; the separate restart
handler heals an RGB panel in place instead of rebooting). A key the file
lacks reads its compiled default, so an older backup lands like an upgrade.
A backup from NEWER firmware is refused via `core::semver_newer` (shared with
the update check), for the widened-enum reason.

Per-unit keys never leave the board, and the repair and boot-state ones are
held across the erase ([which keys](#ev-unit-keys)). The credentials, the
WiFi group and `token`, are OPT-OUT switches on the confirm modal: the file
is plain text on a card that leaves the machine. `mac` and `name` always
travel; they identify the machine, they do not open it.

## Consequences

- Every new setting is a new key with a default and a guard; widening an
  existing enum is a bug even where the current build clamps.
- The Backup page itself (notices, the confirm modal, the Storage page) is
  the ux record `2026-09-12-backup-and-storage-ux.md`; this record owns the
  compatibility rules it rests on.
- Backup and restore run outside the scale-roles persist lock (restore
  reboots; [accepted](#history)).

---

## Details

### Evidence

<a id="ev-fallbacks"></a>
Fallback pairs ([when each was added](#history)):

| Old key | New key | Why |
|---|---|---|
| `updchk` (bool) | `updmode` (int) | on/off became Off / On boot / Daily |
| `ssstyle` (0 Logo, 1 Blank) | `ssstyle2` (0 Logo, 1 Dim, 2 Off) | Dim at index 1 would have flipped every existing user |
| `rdychimev` (linear %) | `rdychimeu` (perceptual) | migrated via `migrate_chime_volume` |
| (new) | `updbeta` | a widened `updmode` would be indexed by older builds |

<a id="ev-unit-keys"></a>
Per-unit keys, kept out of the file by `is_unit_key`: `padsense`,
`paddrive`, `padwire`, `otainst`, `sprpend` / `sprpend2`, `lastunix` and
`_init`. Of those, `padsense`, `paddrive`, `padwire`, `lastunix` and
`otainst` are read out before the erase and written back after it
(`UnitKeys`; `padwire` only since v1.3.0, see [History](#history));
`sprpend` / `sprpend2` are one-boot flags dropped on purpose and `_init` is
re-created at the next boot. The WiFi opt-out group is `ssid`, `wifipass`,
`wifi_en`, plus the static-address keys since v1.0.0-beta.7 (an SSID with no
PSK is a join that can never succeed).

<a id="ev-near-miss"></a>
The hazard is structural; no unit has had to be recovered from it. Nearest
miss: v0.12.1 (2026-09-10, `8d0962b`) widened `updmode` to 3 (Hourly) two
days before the rule was written. v0.12.0's two `kCadence[]` sites computed
the index modulo the array (the cycle button) and clamped it (the Settings
row, `(m >= 0 && m <= 2) ? m : 1`), so a rolled-back v0.12.0 reading a 3
showed "On boot" rather than reading past the end. Rollback is what makes
old-reads-new routine: `verifyRollbackLater()` plus the 60 s healthy-loop
confirm reverts a bad image with nobody present, and betas make that common.

### Alternatives and dead ends

- Restore as a MERGE: rejected; keys the file does not carry would survive,
  so the device after a restore would not be the one the backup described.
- A hand-maintained key list in the backup code: rejected, it drifts.
- Deleting superseded keys: never; a rolled-back build still reads them.

### History

- 2026-09-07: first fallback pair, `updchk` to `updmode` (`9140cf2`).
- 2026-09-10: `ssstyle2` (`8d0962b`).
- 2026-09-11: `rdychimeu` (`4c2beef`).
- 2026-09-12: rule stated in the beta-channel change, `updbeta` added as a
  new key rather than a widened `updmode` (`4379227`); backup and restore
  shipped (`6305121`, `8cea6ad`) in v0.14.0.
- 2026-09-30: static-IP keys join the WiFi opt-out group (`c28f459`,
  v1.0.0-beta.7).
- 2026-10-24: bench-scale keys added as the `2`-suffixed set (`0134038`);
  backup and restore left outside the new scale-roles persist lock, since a
  restore reboots.
- 2026-11-03: `padwire` added to the keys kept out of the file (`455e7c9`)
  but not to the set held across the erase, so a restore on v1.3.0-beta.1 to
  beta.3 reset the wiring choice to Auto.
- 2026-11-04: `padwire` added to `UnitKeys`, so a restore keeps it, for
  v1.3.0 (found by the decision-log audit; issue #8).
- 2026-11-07: folded in from the project's working notes (CLAUDE.md).

### Sources

`9140cf2`, `8d0962b`, `4c2beef`, `4379227`, `6305121`, `8cea6ad`, `c28f459`,
`0134038`, `455e7c9`;
`src/platform_esp32/config.cpp`, `src/platform_esp32/config_backup.cpp`,
`include/core/semver.h`, `src/device/main.cpp` (the backup port, the
reboot handler).
