# Supported hardware

Every board the firmware supports, including the older ones the README no
longer recommends. The short version lives in the README's
[Which board?](../README.md#which-board) table — this page is the full matrix.

## All supported boards

| Board | Display | Wired paddle (Auto shot) | Notes |
|-------|---------|--------------------------|-------|
| **ESP32‑S3‑Touch‑LCD‑2** | 2.0" 240×320, ST7789 (SPI) | — (Shot detect only) | A portable, battery‑friendly remote. |
| **ESP32‑S3‑Touch‑LCD‑4.3B** | 4.3" 800×480, RGB parallel | — (Shot detect only) | Counter‑top panel. Has a PCF85063 RTC. |
| **ESP32‑S3‑Touch‑LCD‑4.3C / 4.3C‑BOX** | 4.3" 800×480, RGB parallel | **Yes — built‑in.** Isolated DI/DO screw terminals (opto‑isolators on board) | **Recommended (easy path).** Dimmable backlight, battery monitoring, PCF85063 RTC, speaker. |
| **ESP32‑S3‑Touch‑LCD‑7B** | 7" 1024×600, RGB parallel | — (Shot detect only) | Large panel. |
| **ESP32‑P4‑WIFI6‑Touch‑LCD‑4.3** | 4.3" 800×480, MIPI‑DSI (ST7701) | **Yes — external opto.** Native GPIOs + a PC817‑style opto module you wire | ESP32‑P4 (32 MB flash / 32 MB PSRAM); WiFi 6 + BLE via on‑board ESP32‑C6. |
| **ESP32‑P4‑WIFI6‑Touch‑LCD‑5** | 5" 1280×720, MIPI‑DSI (HX8394) | **Yes — external opto.** Same wiring as the P4 4.3 | **Recommended (performance path).** Same electronics as the P4 4.3 with a higher‑density panel (UI scaled 1.5×). No enclosure — a printable shell is [in the repo](../hardware/3d-prints/). |
| **ESP32‑P4‑WIFI6‑Touch‑LCD‑X 7"** | 7" 1280×720, MIPI‑DSI (ILI9881C) | **Yes — external opto.** Same wiring as the P4 4.3/5 (GPIO 51/52 on the 40‑pin header) | **Recommended (easy + performance).** Finished box; same electronics as the other P4 boards. |
| **ESP32‑P4‑WIFI6‑Touch‑LCD‑X 8"** | 8" 1280×800, MIPI‑DSI (JD9365) | **Yes — external opto.** Same wiring as the P4 4.3/5 | **Recommended (easy + performance).** Finished box; same electronics as the other P4 boards. UI scaled 1.6×. Not yet verified on hardware. |

Boards without paddle wiring still get the full brew‑by‑weight experience via
**Shot detect** — only the automatic stop at target weight needs the wire.
Step‑by‑step wiring instructions (with photos of the Micra's paddle loom) are
in the **[wiring guide](WIRING.md)**.

The S3 boards use the ESP32‑S3R8 (16 MB flash, 8 MB octal PSRAM); the P4
boards are much faster with 32 MB flash + 32 MB PSRAM. A supported Bluetooth
scale (Bookoo Themis, Acaia Umbra / Lunar / Prochef / Pyxis, or Varia Aku —
Pyxis and Aku untested) is optional but unlocks the shot timer, flow graph,
and brew‑by‑weight features.

## Power, battery, and RTC

The boards run from **USB‑C power** — that's the normal way to use them. An
optional battery can be installed (the boards have a battery connector), but
it only lasts a few hours, so treat it as a nice‑to‑have for moving the device
around, not a way to run it. An optional **RTC coin cell** (boards with an
RTC, e.g. the 4.3B/4.3C) keeps the clock through a power‑off — but it's only
needed if you *don't* configure Wi‑Fi + NTP, which sets the time automatically
on every boot.
