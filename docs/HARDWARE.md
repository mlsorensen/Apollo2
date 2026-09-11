# Supported hardware

The four boards the firmware is built and published for, in detail. The
short version — and the buying advice — lives in the README's
[Which board?](../README.md#which-board) table.

## Supported boards

| Board | Best for | Display | Wired paddle (Auto shot) | Notes |
|-------|----------|---------|--------------------------|-------|
| **ESP32‑P4‑WIFI6‑Touch‑LCD‑5** | Mounting on the Micra | 5" 1280×720, MIPI‑DSI (HX8394) | **Yes — external opto.** Native GPIOs (51/52 on the 40‑pin header) + a PC817‑style opto module you wire | **The default pick.** ESP32‑P4, 32 MB flash / 32 MB PSRAM, WiFi 6 + BLE via an on‑board ESP32‑C6. UI scaled 1.5×. Speaker, microSD, battery monitoring. No enclosure — printable shells are [in the repo](../hardware/3d-prints/). |
| **ESP32‑P4‑WIFI6‑Touch‑LCD‑4.3** | Mounting on the Micra, smaller screen | 4.3" 800×480, MIPI‑DSI (ST7701) | **Yes — external opto.** Same wiring as the P4‑5 | **The same board as the LCD‑5** — identical processor, radio, audio, battery path, SD slot and paddle pins; only the panel differs. Choose between them on screen size and a small price difference. UI at 1:1 (800×480). Its own printed shell, its own firmware image. |
| **ESP32‑S3‑Touch‑LCD‑4.3C / 4.3C‑BOX** | Mounting on the Micra with nothing to build | 4.3" 800×480, RGB parallel | **Yes — built‑in.** Isolated DI/DO screw terminals (opto‑isolators on board), so there is no cable to assemble | **The no‑DIY pick:** the only supported board that is both a finished box *and* pre‑isolated. Slower than the P4s (ESP32‑S3R8, 16 MB flash / 8 MB octal PSRAM). Dimmable backlight, battery monitoring, PCF85063 RTC, speaker, microSD. |
| **ESP32‑P4‑WIFI6‑Touch‑LCD‑X 8"** | Counter‑top companion | 8" 1280×800, MIPI‑DSI (JD9365) | **Yes — external opto.** Same wiring as the P4‑5 | Finished box, biggest screen, same P4 electronics as the LCD‑5. UI scaled 1.6×. Its panel and silicon are verified, but the Apollo image itself is not yet confirmed on one. |

Auto‑shot wiring is optional on every board: without it you still get the full
brew‑by‑weight experience via **Shot detect**, and only the automatic stop at
target weight needs the wire. Step‑by‑step wiring instructions (with photos of
the Micra's paddle loom) are in the **[wiring guide](WIRING.md)**.

A supported Bluetooth scale (Bookoo Themis, Acaia Umbra / Lunar / Prochef /
Pyxis, or Varia Aku — Pyxis and Aku untested) is optional but unlocks the shot
timer, flow graph, and brew‑by‑weight features.

## Power, battery, and RTC

The boards run from **USB‑C power** — that's the normal way to use them. An
optional battery can be installed (the boards have a battery connector), but
it only lasts a few hours, so treat it as a nice‑to‑have for moving the device
around, not a way to run it. An optional **RTC coin cell** (on the 4.3C,
which has one) keeps the clock through a power‑off — but it's only
needed if you *don't* configure Wi‑Fi + NTP, which sets the time automatically
on every boot.
