# The Micra Bluetooth token is minted by a cloud-seed handshake, so Apollo takes it from lmtoken

- **Status:** Accepted
- **Area:** radio
- **First decided:** 2026-08-24 · **Last changed:** 2026-08-29
- **Boards:** all

## Context

Apollo authenticates to the Micra by writing an opaque token to the auth
characteristic (`0d0b7847-...`) on every connect. The token is DERIVED BY
THE MACHINE from an encrypted seed minted by the cloud; it is not stored
for a cold read, and the cloud only learns it when the pairing client
uploads it. So nothing can read the token off the machine, and an account
whose pairing never reached the cloud shows an empty `bleAuthToken` ([how
this was established](#evidence)).

## Decision

- The handshake is cloud `start` (an encrypted seed) -> machine in
  CONFIGURATION mode -> write the seed, read back a 64-hex token -> cloud
  `confirm`. A machine already bound to the account refuses the confirm, so
  a re-token is `unpair -> start -> BLE -> confirm` ([the steps, UUIDs and
  paddle sequence](#dd-handshake), [proven end to end](#evidence)).
- No auto-fetch in the firmware: `0c` is the seed slot and can never yield
  a token by reading ([the retired design](#alternatives-and-dead-ends)).
  The token is always entered through the WiFi setup page.
- `lmtoken` provisions one over Bluetooth when the cloud token is blank (or
  with `-force`), unpairing and re-pairing as needed, and matches the
  machine by service UUID. The README leads with "get your token first" so
  nobody buys a board they cannot set up.
- Apollo does not self-provision: the seed needs the account's signed cloud
  call, so the credentials would have to live on the device ([deferred, and
  why](#alternatives-and-dead-ends)).

## Consequences

- On a wired rig the paddle reaches the machine only while Apollo is on and
  relaying (Manual mode), so entering config mode needs Apollo passing
  through.
- A machine left in config mode cannot be connected until restarted; the
  link reports it ("needs restart").
- "Cloud mandatory" is an inference from the seed's shape, [not a tested
  fact](#alternatives-and-dead-ends).

---

## Details

### Design detail

<a id="dd-handshake"></a>

1. `POST customer-app/thing-pairing/start {serialNumber}` returns an
   encrypted `pairSeed` (~344 base64 characters, ~258 raw bytes, the size of
   one RSA-2048 block).
2. The machine must be in CONFIGURATION mode: power off, paddle left, power
   on, wait 5 s, paddle right. It then advertises service `D10A7847-...`
   instead of the operative `D30A7847-...`, exposes the seed characteristic
   `0c` (read/write, empty) and `enableConfiguration` (`dc0a7847-...`), and
   hides the auth characteristic. A PAIRED machine enters this mode too; it
   is not a short window.
3. Write one byte to `enableConfiguration`, write the seed to `0c`, read
   `0c` back: a 64-hex token, immediately.
4. `POST thing-pairing/confirm` uploads it as both `pairToken` and
   `bleAuthToken`. On a machine already bound to the account this returns
   `THING_ALREADY_PAIRED` (`removeOthers` does not bypass it), so a
   re-token is `unpair -> start -> BLE -> confirm`.

### Evidence

Worked out 2026-08-24 from live BLE captures and GATT dumps of the bench
machine (firmware 1.17, gateway 7.1.6, per the working notes) and proven
end-to-end the same day:

| Fact | How established | Date |
|---|---|---|
| Paired machine drops to config mode on the paddle sequence; service flips `d30a7847 -> d10a7847` and stays | GATT dump | 2026-08-24 |
| `0c` present but empty in config mode; no `0d` auth char there | GATT dump | 2026-08-24 |
| Seed write -> 64-hex token in one read; `confirm` stores it; `/things` read-back matches | proof-of-concept script (not in the repo; its logic became `tools/lmtoken/provision.go`) | 2026-08-24 |
| Plain `confirm` on a bound machine -> HTTP 400 `THING_ALREADY_PAIRED` | the same script | 2026-08-24 |
| User with the blank token provisioned and connected | issue #1 | 2026-09-19 |

The field report (issue #1, 2026-08-22, a user on a P4-4.3 with firmware
0.7.0) showed both of the earlier paths failing: the auto-fetch bounced
`NeedsToken -> Connecting -> NeedsToken`, and the `lmtoken` cloud tool
found the machine but the account's `bleAuthToken` was empty.

Empty-token root cause: the seed -> derive -> confirm cycle never reached
the cloud for that machine and account. The confirm body carries
`offlineMode`, so a pairing done offline derives a working token for the
app while leaving the cloud field blank. This is an inference from the
protocol, not something observed on the user's machine.

### Alternatives and dead ends

- Reading the token cold: the original design from 2026-06-28 connected in
  pairing mode and read characteristic `0c0b7847-...`, believed to be a
  plain token getter; it never worked for anyone and was retired in v0.8.0
  (87c5187). The QR label inside the chassis is a `pairToken`-class value,
  never decoded here; deprioritised.
- Cloudless pairing with a self-made seed: never tested. The seed's size and
  the fact that only the seed is encrypted point to the machine validating a
  cloud signature offline; "cloud mandatory" is inference.
- On-Apollo provisioning (WiFi + BLE on the device): deferred 2026-08-24. It
  needs the cloud client in C++/mbedTLS and TLS buffers on RAM-tight boards.

### History

- 2026-06-28: auto-read of `0c` in pairing mode, WiFi page as fallback (45861f9, 5528c19).
- 2026-08-22: issue #1 opened.
- 2026-08-24: handshake reverse-engineered and proven; auto-fetch retired (87c5187); lmtoken v0.2.0 provisioning (4cd271c, lmtoken-v0.2.0); firmware v0.8.0.
- 2026-08-25: lmtoken hands over the token even when the cloud save fails (26a897f, lmtoken-v0.2.1).
- 2026-09-19: issue #1 closed by the reporter.

### Sources

- Commits 45861f9, 5528c19, 87c5187, 4cd271c, 26a897f; tags v0.8.0, lmtoken-v0.2.0, lmtoken-v0.2.1.
- Issue #1.
- `tools/lmtoken/provision.go`, `tools/lmtoken/pairing.go`, `src/core/micra_link.cpp`.
