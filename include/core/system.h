#pragma once

#include <cstdint>

// Tiny environment shims so core protocol code can run anywhere. Declared here,
// implemented once per platform (src/platform_esp32/system.cpp wraps
// millis/vTaskDelay/Serial; src/platform_host/system.cpp wraps chrono/stdio).

namespace core {

// Milliseconds since boot/start. Monotonic; wraps like a uint32.
uint32_t now_ms();

// Block the CALLING thread for `ms` (a FreeRTOS task delay on-device — never
// call from the UI thread with anything long).
void sleep_ms(uint32_t ms);

// printf-style diagnostic log. No implicit newline.
void logf(const char* fmt, ...);
// "Detailed log" (Settings > Apollo > Display): the steady-state chatter —
// per-minute perf line, saver/paddle/update-check housekeeping, finder and
// detector traces, IDF's info-level lines — is written only while this is
// on. Event lines (links, shots, dose, schedule, memory tripwires, errors)
// always are. Live-switchable; the platform also sizes the ring from it at
// boot (512 KB on / 128 KB off, PSRAM).
bool log_detail();
void set_log_detail(bool on);

}  // namespace core
