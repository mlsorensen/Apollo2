#include "core/system.h"

#include <Arduino.h>

#include <cstdarg>
#include <cstdio>

// ESP32 implementations of the core environment shims. sleep_ms is a FreeRTOS
// task delay (yields the calling task only); logf goes to the USB serial
// console like every other diagnostic in the platform layer.

namespace core {

uint32_t now_ms() { return millis(); }

void sleep_ms(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

void logf(const char* fmt, ...) {
  char buf[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  Serial.print(buf);
}

}  // namespace core
