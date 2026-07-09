#include "core/system.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <thread>

// Host implementations of the core environment shims, so portable core code
// (the BLE protocol links) compiles and runs in the simulator build.

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
  va_list args;
  va_start(args, fmt);
  std::vprintf(fmt, args);
  va_end(args);
}

}  // namespace core
