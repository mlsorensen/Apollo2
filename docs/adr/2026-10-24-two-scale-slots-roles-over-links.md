# Two scale slots: the slot is the role, and roles sit over links

- **Status:** Accepted
- **Area:** radio
- **First decided:** 2026-10-24 · **Last changed:** 2026-10-30
- **Boards:** all

## Context

A second scale on the counter (a plate hides the scale's own display)
needs its weight readable on Home, with no shot role. "Just add a second
link" is wrong twice over: the code needs a notion of which scale does the
shot work, and a `platform::ScaleLink` is bound to its radio slot, its task
and, for a paired Umbra, the partner GATT service, none of which can move at
runtime, so "swap the two scales" cannot be a swap of links.

## Decision

- Two SLOTS named by role. ESPRESSO holds every shot role (auto-stop, the
  tare at shot start, shot detect, the auto-flush cup-off, the graph); BENCH
  is a weight readout with its own Tare, never refused mid-shot ([why a
  full `IScale`](#alternatives-and-dead-ends)).
- The user picks the slot at save time, every time: Scan, tap a result,
  "Use <name> as which scale?". No auto-slotting; a one-scale user taps
  Espresso and never sees a bench row ([why not a
  registry](#alternatives-and-dead-ends)).
- ROLES OVER LINKS (`core::ScaleRoles`): consumers read "the espresso
  scale" through a `RoleScale` proxy. "Swap roles" flips the two proxies AND
  exchanges the stored NVS records, so a rolled-back build still finds its
  espresso scale under the original keys, and both links keep their
  connections ([mechanism](#dd-roles), [measured](#evidence)).
- The espresso keys keep their names; the bench set is the same names
  suffixed `2`. All ADDs ([keys](#dd-keys)).
- The flip and the key exchange run under ONE lock,
  `ScaleProvisioner::swap_mu_`; every per-link persister goes through
  `persist_for(link, fn)` under it ([why ordering alone is not
  safe](#alternatives-and-dead-ends), [lock discipline](#dd-lock)).
- Umbra partner mode works in either slot, one at a time; a second is
  refused and saved as a plain link.

## Consequences

- The shot park follows the ROLE: after a swap the bench role sits on radio
  slot kScaleEspresso and main.cpp parks that slot; the espresso role is never
  shot-parked.
- `link_for(slot)` / `slot_of(link)` are the only role-to-link lookups.
- A save or forget that overtakes a connect drops that connect's learned
  type.
- Max 3 connections (prebuilt NimBLE): Micra + two scales, or Micra + one
  inbound partner Umbra + one scale. Two partner Umbras are deferred.
- Cost: ~24 B .bss for the roles, 8 B for the lock, and the second link's
  share of the S3's DMA-capable pool ([measured](#evidence)).

---

## Details

### Design detail

<a id="dd-roles"></a>
The `RoleScale` proxy is an atomic pointer to whichever link holds the
role; BrewController, Home and Device settings all read through it. At boot
the mapping is identity (link A seeded from the espresso keys). A swap is
no reconnect and no restart.

<a id="dd-keys"></a>
Espresso: `smac`, `sname`, `sprtnr`, `sprpend`, `satype`, `sbatest`. Bench:
the same names suffixed `2`.

<a id="dd-lock"></a>
`persist_for(link, fn)` takes `swap_mu_`, resolves `slot_of` and writes;
the persisters today are the learned address type and the battery blob.
`save_scale`, `forget` and `set_partner_mode` hold the lock too. It is the
OUTERMOST lock (links, arbiter, NVS run under it); a persister is called
holding nothing. Backup/restore stay outside it (restore reboots; accepted).

### Evidence

| Measurement | Value | Where / when |
|---|---|---|
| `core::ScaleRoles` | ~24 B .bss, two vtables in flash | every env, 2026-10-24 (0316f74) |
| `swap_mu_` | +8 B .bss (112,244 -> 112,252), ~100 B heap on first lock | S3 4.3C, 2026-10-30 (740a54c) |
| S3 4.3C DMA-capable free, v1.1.0 -> branch | 37,004 -> 31,212 idle bench link -> 28,836 both connected; lowest 24.4 KB, largest 25.6 KB | bench A/B, 2026-10-24 (the S3 go; per the working notes) |
| Swap with Umbra partner (espresso) + Lunar (bench) | zero reconnects | P4-5, 2026-10-24 (per the working notes) |
| Micra + inbound Umbra + Lunar | Micra connected in 6 s | P4-5, 2026-10-24 (per the working notes) |

### Alternatives and dead ends

- A multi-scale registry with a last-used marker, planned 2026-07-18 and
  dropped the same day ("no reason to remember multiple
  scales"). Two fixed roles are not that.
- Ordering the swap's two steps instead of locking. 0316f74 (2026-10-24)
  flipped pointers before keys, claiming a mid-swap write then hits the
  link's own record. Issue #6 showed neither order is safe: pointers-first
  writes link A's value onto B's record and the key swap carries it to B's
  new role; keys-first does the same the other way. Fixed by the lock
  (740a54c, 30a6317, v1.2.0-beta.4).
- A UI-only bench readout fed from the platform: rejected so a later dose
  workflow could treat the bench slot as a full `core::IScale` (it did, in
  v1.3.0).

### History

- 2026-07-18: a multi-scale registry dropped.
- 2026-10-24: design settled (until v1.1 Apollo knew one scale); second link + slot-aware provisioner (0134038), roles over links (d075cdd), review fixes (0316f74); v1.2.0-beta.1/2.
- 2026-10-30: issue #6 fixed, `swap_mu_` + `persist_for` (740a54c), save/forget/partner RMW under the lock, a save or forget overtaking a connect drops its learned type (30a6317); v1.2.0.

### Sources

- Commits 0134038, d075cdd, 0316f74, 740a54c, 30a6317; tags v1.2.0-beta.1, v1.2.0; issue #6.
- `include/core/scale_roles.h`, `include/core/scale_provisioner.h`, `src/platform_esp32/scale_provisioner.cpp`, `src/device/main.cpp`.
- UX side: `2026-10-24-two-scales-home-and-settings.md`.
