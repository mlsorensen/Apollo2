#pragma once

// Boot-flag OTA install mode. When config.pending_install() is set, main runs
// this VERY EARLY in setup() — before the full display / BLE / app come up — so
// the internal-DMA pool is at its clean baseline (the hosted-radio bulk
// download needs a ~64 KB contiguous block the full display fragments away).
// It brings up a lightweight display (progress UI), downloads the target image
// into PSRAM, then writes it to the spare OTA slot with the screen blanked
// (flash writes glitch the DSI scanout) and reboots into it. Never returns.
// See the [[ota-p4-solution]] memory for the full rationale.

namespace platform {

class Config;

namespace install_mode {

// Perform the pending install and reboot. Works on DSI (light panel) and RGB
// (full display) boards; a no-op that clears the flag on the SPI 2-inch (no
// real update UI there). Never returns when an install runs.
void run(Config& config);

}  // namespace install_mode
}  // namespace platform
