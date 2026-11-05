# Backup and Storage pages: opt-out credentials, restore reboots, Mount is a tap

- **Status:** Accepted
- **Area:** ux
- **First decided:** 2026-09-12 · **Last changed:** 2026-11-04
- **Boards:** boards with an SD slot (all released boards); the page is absent elsewhere

## Context

Settings live in NVS and shot history on the card, so swapping hardware
carried every shot and lost every preference. Separately, on the P4 boards a
cardless slot probed every 5 s disturbed the radio badly enough to restart
the device (root cause and mechanism in the platform records; shipped in
v1.0.3-beta.1, first stable release v1.1.0; there is no v1.0.3 tag). Both
needed a page and a policy a user can follow.

## Decision

**Settings > Apollo > Backup** (2026-09-12, v0.14.0):

- "On the card" says what the inserted card holds: when, which firmware and
  board, and whether it carries the WiFi network and the pairing token.
- "Back up to card" writes `/Apollo2/settings.txt`. The confirm modal has two
  OPT-OUT switches, on by default: the WiFi network (SSID, password and the
  enable flag together, since an SSID without its key is a join that can
  never succeed) and the Micra pairing token. Default on because the point
  of a backup is a replacement that just works; off is the trade when the
  card will not stay with the machine. The machine's address and name
  always travel: they identify it, they do not open it.
- "Restore from card" is a REPLACE, then a full reboot. A setting the file
  does not carry goes back to its default, so an older backup lands exactly
  like an upgrade. A backup from NEWER firmware is refused; the user
  updates first. Greyed out without a backup on the card.
- Nothing happens on its own: no automatic backup or restore.

**Settings > Apollo > Storage** (2026-10-16, v1.0.3-beta.1, in v1.1.0):

- The slot is checked ONCE at startup and otherwise only when the user taps
  Mount. The status row reads Mounted, No card, Unsupported format, Removed
  (pulled while mounted; nothing lost, stays unmounted) or Unmounted, safe
  to remove; the button beside it is Mount or Unmount.
- A Mount tap is one ~25 ms exposure on the user's say-so; the radio is not
  torn down for it (a deliberate choice). The page says why in a short note.
- Card (make, capacity, filesystem) and Space rows; the whole chooser fits
  800x480 as a fifth row without scrolling.

## Evidence

- A cardless probe hit the shared clock divider ~1 in 1,500 times on the
  bench X-8 (2026-10-16); a mounted card never did. The one-tap policy
  removes the periodic probe entirely.
- Restore's refusal rule is the same reasoning as the NVS compatibility
  rule: an older build indexes stored enums into fixed arrays.

## Alternatives and dead ends

- A second mount or task for the settings file: not needed, the shot
  store's writer task already owns the card; backup and restore are two more
  job kinds on its queue.
- A hand-kept list of keys to back up: rejected; NVS is enumerated so a
  setting added later travels without anyone remembering the file.
- A merge on restore: rejected in favour of the replace, which is what makes
  an old backup behave like an upgrade.
- Keeping the 5 s cardless retry: must not come back (CLAUDE.md "SD mount
  policy").

## Consequences

- Per-unit keys never leave the board (`is_unit_key` in
  `config_backup.cpp`: `padsense`, `paddrive`, `padwire`, `otainst`,
  `sprpend`, `sprpend2`, `lastunix`, `_init`). Of those, the paddle
  repair knobs, the paddle wiring choice, the install flag and the clock
  seed are lifted out before the erase and put back (`read_unit_keys` /
  `write_unit_keys`); the Umbra pairing-prompt flags are dropped by design.
  The wiring choice (`padwire`) was added to the list on 2026-11-03 without
  the hold, so a restore on the first three v1.3.0 betas reset it to Auto;
  fixed 2026-11-04 (issue #8).
- Screenshots: `../img/manual/device-backup.png`,
  `../img/manual/backup-modal.png`, `../img/manual/device-storage.png`.

## History

- 2026-09-12: Backup page (`6305121`, v0.14.0).
- 2026-10-16: Storage page with Mount/Unmount; probe once at boot
  (`8946ad6`, v1.0.3-beta.1).
- 2026-11-03: `padwire` joins the unit-key list (`455e7c9`), without the
  hold across the erase.
- 2026-11-04: `padwire` held across the erase like the repair knobs
  (issue #8).

## Sources

Commits `6305121`, `8946ad6`, `455e7c9`; `src/platform_esp32/config_backup.cpp`;
`src/ui/settings_tab.cpp` (the page rows); CLAUDE.md "Settings backup to the
card", "SD mount policy"; MANUAL.md "Backup", "Storage".
