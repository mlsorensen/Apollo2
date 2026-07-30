#pragma once

// Boot-time wiring for core::log_ring() on ESP32: allocates the ring storage
// (PSRAM when present, a smaller internal fallback otherwise), points the echo
// sink at Serial, and hooks esp_log so IDF/NimBLE tags land in the ring too.
// Call once, early in setup() — before the first diagnostic print.

namespace platform {

void log_init();

}  // namespace platform
