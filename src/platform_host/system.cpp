#include "core/system.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <thread>

#include "core/log_ring.h"

// Host implementations of the core environment shims, so portable core code
// (the BLE protocol links) compiles and runs in the simulator build. logf
// feeds the same core::log_ring() the device uses (echoed to stdout), which
// lets the sim render the log-viewer modal with real content.

namespace core {

uint32_t now_ms() {
  using namespace std::chrono;
  static const steady_clock::time_point start = steady_clock::now();
  return static_cast<uint32_t>(
      duration_cast<milliseconds>(steady_clock::now() - start).count());
}

void sleep_ms(uint32_t ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void logf(const char* fmt, ...) {
  LogRing& ring = log_ring();
  if (!ring.ready()) {
    static char storage[64 * 1024];
    ring.init(storage, sizeof(storage));
    ring.set_sink([](const char* s, size_t n) { std::fwrite(s, 1, n, stdout); });
  }
  char buf[256];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  ring.write(buf);
}

}  // namespace core
