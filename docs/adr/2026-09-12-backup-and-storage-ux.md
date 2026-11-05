# Backup and Storage pages: opt-out credentials, restore reboots, Mount is a tap

- **Status:** Accepted
- **Area:** ux
- **First decided:** 2026-09-12 · **Last changed:** 2026-11-04
- **Boards:** boards with an SD slot (all released boards); the page is absent elsewhere

## Context

Settings live in NVS and shot history on the card, so swapping hardware
carries every shot and loses every preference unless the settings can
travel too. On the P4 boards the SD slot shares its controller with the
radio, and a cardless slot probed on a timer disturbs the radio badly
enough to restart the device
(`2026-07-25-sd-slot-shares-the-radio-host.md`; [how often](#evidence)).
Both need a page and a policy a user can follow.

## Decision

**Settings > Apollo > Backup** (shipped in v0.14.0):

- "On the card" says what the inserted card holds: when, which firmware and
  board, and whether it carries the WiFi network and the pairing token.
- "Back up to card" writes `/Apollo2/settings.txt`. The confirm modal has two
  OPT-OUT switches, on by default ([why on](#alternatives-and-dead-ends)):
  the WiFi network (SSID, password and the enable flag together) and the
  Micra pairing token. The machine's address and name always travel: they
  identify it, they do not open it.
- "Restore from card" is a REPLACE, then a full reboot
  ([why not a merge](#alternatives-and-dead-ends)): a setting the file does
  not carry goes back to its default, so an older backup lands like an
  upgrade. A backup from NEWER firmware is refused ([why](#evidence)).
  Greyed out without a backup on the card.
- Nothing happens on its own: no automatic backup or restore.

**Settings > Apollo > Storage** (shipped in v1.0.3-beta.1; first stable
release v1.1.0):

- The slot is checked ONCE at startup and otherwise only when the user taps
  Mount ([why no retry loop](#alternatives-and-dead-ends)). The status row
  reads Mounted, No card, Unsupported format, Removed (pulled while
  mounted; nothing lost) or Unmounted, safe to remove; the button beside it
  is Mount or Unmount.
- A Mount tap is one ~25 ms exposure on the user's say-so; the radio is not
  torn down for it. The page says why in a short note.
- Card (make, capacity, filesystem) and Space rows; the whole chooser fits
  800x480 as a fifth row without scrolling.

## Consequences

- Per-unit keys never leave the board ([the list](#ev-unit-keys)); the
  paddle repair knobs, the paddle wiring choice, the install flag and the
  clock seed are held across the erase, the Umbra pairing-prompt flags are
  dropped by design ([the betas that lost the wiring choice](#history)).
- Screenshots: `../img/manual/device-backup.png`,
  `../img/manual/backup-modal.png`, `../img/manual/device-storage.png`.

---

## Details

### Evidence

- A cardless probe hit the shared clock divider ~1 in 1,500 times on the
  bench X-8 (2026-10-16); a mounted card never did. Until then the slot was
  probed every 5 s. The one-tap policy removes the periodic probe entirely.
- Restore's refusal rule is the same reasoning as the NVS compatibility
  rule (`2026-09-07-nvs-add-only-settings-compat.md`): an older build
  indexes stored enums into fixed arrays.

<a id="ev-unit-keys"></a>
The unit keys, `is_unit_key` in `config_backup.cpp`: `padsense`,
`paddrive`, `padwire`, `otainst`, `sprpend`, `sprpend2`, `lastunix`,
`_init`. Of those, `padsense`, `paddrive`, `padwire`, `otainst` and
`lastunix` are lifted out before the erase and put back (`read_unit_keys` /
`write_unit_keys`); `sprpend` and `sprpend2` (the Umbra pairing-prompt
flags) are dropped by design.

### Alternatives and dead ends

- A second mount or task for the settings file: not needed, the shot
  store's writer task already owns the card; backup and restore are two more
  job kinds on its queue.
- A hand-kept list of keys to back up: rejected; NVS is enumerated so a
  setting added later travels without anyone remembering the file.
- A merge on restore: rejected in favour of the replace, which is what makes
  an old backup behave like an upgrade.
- Credentials off by default: rejected; the point of a backup is a
  replacement that just works, and off is the trade when the card will not
  stay with the machine. The SSID travels with its key and enable flag
  because an SSID without its key is a join that can never succeed.
- Tearing the radio down for a Mount tap: rejected, a deliberate choice;
  one ~25 ms exposure on the user's say-so is accepted.
- Keeping the 5 s cardless retry: must not come back (CLAUDE.md "SD mount
  policy").

### History

- 2026-09-12: Backup page (`6305121`, v0.14.0).
- 2026-10-16: Storage page with Mount/Unmount; probe once at boot
  (`8946ad6`, v1.0.3-beta.1; there is no v1.0.3 tag, the first stable
  release with it is v1.1.0).
- 2026-11-03: `padwire` joins the unit-key list (`455e7c9`), without the
  hold across the erase, so a restore on the first three v1.3.0 betas reset
  it to Auto.
- 2026-11-04: `padwire` held across the erase like the repair knobs
  (issue #8).

### Sources

Commits `6305121`, `8946ad6`, `455e7c9`; issue #8;
`src/platform_esp32/config_backup.cpp`; `src/ui/settings_tab.cpp` (the page
rows); CLAUDE.md "Settings backup to the card", "SD mount policy"; MANUAL.md
"Backup", "Storage".
