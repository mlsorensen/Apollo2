#include "core/system.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_log.h>

#include <cstdarg>
#include <cstdio>

#include "core/log_ring.h"
#include "platform_esp32/log_setup.h"

// ESP32 implementations of the core environment shims. sleep_ms is a FreeRTOS
// task delay (yields the calling task only); logf feeds core::log_ring(),
// whose echo sink writes the stamped text to the USB serial console — so the
// ring and the serial output stay byte-identical.

namespace core {

uint32_t now_ms() { return millis(); }

void sleep_ms(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

void logf(const char* fmt, ...) {
  char buf[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  LogRing& ring = log_ring();
  if (ring.ready()) {
    ring.write(buf);  // echoes to Serial via the sink, stamped
  } else {
    Serial.print(buf);  // pre-log_init() fallback (earliest boot lines)
  }
}

}  // namespace core

namespace platform {

namespace {

// IDF log capture: stash each formatted line in the ring (echo suppressed —
// the chained vprintf prints exactly what it always did, so the UART/USB
// console output is unchanged).
vprintf_like_t s_chain_vprintf = nullptr;

int idf_log_to_ring(const char* fmt, va_list args) {
  char buf[256];
  va_list copy;
  va_copy(copy, args);
  const int n = vsnprintf(buf, sizeof(buf), fmt, copy);
  va_end(copy);
  // IDF's own info/debug lines are chatter; warnings and errors always go in.
  const bool info = n > 0 && (buf[0] == 'I' || buf[0] == 'D' || buf[0] == 'V');
  if (n > 0 && (!info || core::log_detail())) core::log_ring().write(buf, /*echo=*/false);
  return s_chain_vprintf != nullptr ? s_chain_vprintf(fmt, args) : n;
}

}  // namespace

void log_init(bool detailed) {
  // PSRAM keeps a generous window, sized by the Detailed log setting (read
  // from NVS before this; a resize takes the next boot): 512 KB with the
  // per-minute chatter on (~2 days), 128 KB without (events only — weeks).
  // The internal fallback still covers a full boot plus several minutes.
  const size_t kPsramCap = (detailed ? 512 : 128) * 1024;
  constexpr size_t kInternalCap = 12 * 1024;
  size_t cap = kPsramCap;
  char* buf = static_cast<char*>(
      heap_caps_malloc(kPsramCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (buf == nullptr) {
    cap = kInternalCap;
    buf = static_cast<char*>(malloc(kInternalCap));
  }
  core::LogRing& ring = core::log_ring();
  ring.set_sink([](const char* s, size_t n) {
    Serial.write(reinterpret_cast<const uint8_t*>(s), n);
  });
  if (buf != nullptr) ring.init(buf, cap);
  s_chain_vprintf = esp_log_set_vprintf(idf_log_to_ring);
  core::logf("log: ring %u KB (%s); heap lines are free/largest/lowest per pool\n",
             static_cast<unsigned>(cap / 1024), cap == kPsramCap ? "PSRAM" : "internal");
}

}  // namespace platform
