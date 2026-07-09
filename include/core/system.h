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

}  // namespace core
