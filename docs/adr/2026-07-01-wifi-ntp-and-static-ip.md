# Optional WiFi station with NTP time, set up only on the portal, including a static address

- **Status:** Accepted
- **Area:** platform
- **First decided:** 2026-07-01 · **Last changed:** 2026-09-30
- **Boards:** all (native WiFi on the S3 boards, via the C6 on the P4 boards)

## Context

The remote needs the time of day for shot history, the schedule and the
update check, and a network for the web UI and self-update. It must also
work with no network at all, and never leave a user locked out of a device
that has the wrong credentials.

## Decision

- WiFi is an optional station (`INetwork` port; off by default) with SNTP
  and a POSIX-TZ time zone from a city table. SNTP retries fast until the
  first real sync lands, then hourly (`kNtpSyncIntervalMs`). On the RTC
  boards the full local date is stored in the PCF85063 so the clock survives
  a power cycle.
- Credentials are entered on the same AP setup portal as the Micra token.
  The form is fire-and-forget: the device drops the AP, connects as a
  station, and the device screen is the source of truth. There is no
  concurrent AP/STA. Failed credentials reopen the AP, and "Set up WiFi"
  always raises it.
- Modem sleep is off (`WiFi.setSleep(false)`) on every board; it matters
  only on the S3's native radio, the P4's WiFi being on the C6.
- A static address is set only on the portal's WiFi page ("Network address":
  DHCP default, Static reveals IP, mask, gateway, optional DNS which
  defaults to the gateway). Both the
  page's regex and `IPAddress::fromString` validate; a bad set is refused
  whole. There is deliberately no on-device editor: change or clear it by
  running Set up WiFi again, and the page pre-fills the stored values.
  `start_station` applies `WiFi.config()` before `begin()`, or an all-zero
  config to return to DHCP in the same boot.

## Evidence

- 2026-09-08, 4.3C at -51 dBm: with the Arduino default `WIFI_PS_MIN_MODEM`
  ping was spotty and `getaddrinfo()` timed out (~14 s) because the UDP DNS
  reply arrived while the modem slept; this was the long-standing "NTP rarely
  works on the S3". With sleep off ping was stable and the check fetched on
  the first retry.
- 2026-09-09: the update check's "network ready" gate on any plausible time
  opened ~1 s after DHCP on RTC boards and ran before DNS worked; a real SNTP
  reply is now the proof that DNS and a UDP round trip both work (shared with
  the schedule engine's clock trust).

## Alternatives and dead ends

- Concurrent AP + STA for setup: rejected 2026-07-01; the form is
  fire-and-forget and the device screen (`NetState`) is the one source of
  truth, so there is no second state to keep in step.
- An on-device static-IP editor: rejected on 2026-09-30; a
  wrong address typed on the glass is harder to recover than on the portal.
- Listening ports in normal operation beyond the web UI: none; the setup
  portal is an open AP only while setup runs.

## Consequences

- NVS keys, adds only: `wifi_en`, `ssid`, `wifipass`, `tz`, `ntp`, `ntp_en`,
  and for static addressing `ipstatic`, `ipaddr`, `ipmask`, `ipgw`, `ipdns`. The
  credentials and the static set travel in the settings backup's WiFi
  opt-out group, since an SSID with no PSK is a join that cannot succeed.
- `INetwork::static_ip()` feeds the Info row "x.x.x.x (static|DHCP)".
- The P4's radio is a separate chip, so the S3's sleep and heap findings do
  not transfer to it.

## History

- 2026-07-01: WiFi station + NTP, portal flow (`170949f`).
- 2026-09-08: modem sleep off on the S3 (`8a2ff61`, v0.11.4).
- 2026-09-09: network-ready gate on a landed SNTP reply (`7760dfe`).
- 2026-09-30: static IP on the portal (`c28f459`); the Info row says DHCP
  or static (`ea74db8`); v1.0.0-beta.7.

## Sources

Commits `170949f`, `8a2ff61`, `7760dfe`, `c28f459`, `ea74db8`;
`src/platform_esp32/network.cpp`; CLAUDE.md "Static IP".
