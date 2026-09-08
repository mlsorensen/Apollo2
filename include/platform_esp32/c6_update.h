#pragma once

#include <functional>

// ESP32-C6 esp-hosted SLAVE firmware self-update. The C6 co-processor that
// carries WiFi+BLE over SDIO ships from the factory with a stale slave image
// that doesn't match the host esp-hosted library pinned by our platform (the
// version RPC times out every boot, and — the reason this exists — its RX
// buffer handling asserts under a sustained download, which blocks on-device
// OTA). The matching image (esp32c6-v<host>.bin) is EMBEDDED in the app, so we
// can reflash the C6 with no internet: the transfer is host->slave TX over
// SDIO, which works even while the stale slave's RX path is broken. Every
// Apollo flash upgrades the C6 on first boot, for free.
//
// Hosted boards only (guarded by CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE); a no-op
// returning false everywhere else.

namespace platform {

// True when the running slave firmware is older than the embedded image (or
// absent/unreadable — the stale factory slave never answers the version RPC).
// Requires the hosted link already up (hostedInitBLE()).
bool c6_update_needed();

// Stream the embedded slave image to the C6 and activate it. progress(0..100)
// is called as it goes (drive the UI from there — this blocks for the whole
// ~1.2 MB flash write). Returns true if the slave was flashed + activated, in
// which case the caller MUST reboot the P4 so the hosted link re-inits against
// the new slave. Returns false if not needed or on error (the radio is left
// usable on the old slave). Self-limits: after kMaxTries consecutive failed
// boots it gives up and returns false so a bad image can't boot-loop.
bool c6_update_apply(const std::function<void(int)>& progress);

}  // namespace platform
