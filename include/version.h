#pragma once

// This remote's own firmware version — the software running on the ESP32, NOT the
// Micra. Shown in Stats > Info ("Remote FW") so a flashed device is identifiable.
// Bump kVersion on releases; the build also stamps the git revision (FW_GIT_REV,
// see tools/pio_version.py) so dev builds are traceable to a commit.

namespace fw {

inline constexpr const char* kVersion = "1.1.0-beta.6";

// What lies under the app on this board (pinned bootloader + partition table),
// which OTA can't change. Comes from platformio.ini `custom_flash_epoch` via
// tools/pio_flash_layout.py; a release whose meta.json lists a higher epoch for
// this board's slug must be web-flashed (update_check.cpp). The sim has none.
#ifdef APOLLO_FLASH_EPOCH
inline constexpr int kFlashEpoch = APOLLO_FLASH_EPOCH;
#elif defined(ARDUINO)
// A device build without it would silently claim epoch 1 and offer OTAs it
// can't install (it happened: the define was once added too late to reach
// the sources). Fail the build instead.
#error "APOLLO_FLASH_EPOCH not defined: set custom_flash_epoch on this env"
#else
inline constexpr int kFlashEpoch = 1;  // host sim
#endif

#ifdef FW_GIT_REV
inline constexpr const char* kGitRev = FW_GIT_REV;
#else
inline constexpr const char* kGitRev = "";
#endif

}  // namespace fw
