#pragma once

// ESP32-P4 L2 cache compatibility (src/platform_esp32/l2_cache_compat.cpp).
// One boot-log line with the L2 geometry the hardware is ACTUALLY running
// (size / ways / line), read back from the cache controller — the restore at
// CORE init sets it, this proves it. No-op on other chips.
namespace platform {
void log_l2_cache_mode();
}
