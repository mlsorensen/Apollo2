// ESP32-P4: keep the heap out of RAM that a pre-3.3.12 bootloader maps as L2 cache.
//
// Companion to tools/pio_p4_l2_compat.py (issue #4). The core's prebuilt libs
// (3.3.12+) assume a 128 KB L2 cache; every fielded unit's bootloader (from a
// <= 3.3.11 web flash) configured 256 KB, which is still in force while the
// app loads and its heap starts. The linker fix keeps our code/data clear of
// the extra 128 KB, but the libs' heap table still hands that window out — on
// a rev v3 unit heap_caps_init faulted writing it (Store access fault at
// 0x4ff20024, bench 2026-10-18); on rev v1.x (cache at the top of L2MEM) it
// would be handed out mid-heap and fault later. Reserving it costs nothing
// against <= 3.3.11 builds, which never had it.
//
// Not reclaimable later either: the app does switch the cache to 128 KB during
// startup, but a write to the window from setup() still takes a Store access
// fault (bench 2026-10-18) — it sits outside the app's linked memory map, so
// the CPU's memory protection refuses it. Leave it reserved.
#include "platform_esp32/l2_cache_compat.h"

#include <sdkconfig.h>

#include "core/system.h"

#if CONFIG_IDF_TARGET_ESP32P4 && CONFIG_CACHE_L2_CACHE_SIZE < 0x40000
#include <esp_attr.h>
#include <esp_private/startup_internal.h>
#include <freertos/FreeRTOS.h>
#include <heap_memory_layout.h>
#include <rom/cache.h>

#if CONFIG_ESP32P4_SELECTS_REV_LESS_V3
// rev v1.x: heap region ends 0x4ff80000 under a 256 KB cache, 0x4ffa0000 under 128 KB.
SOC_RESERVE_MEMORY_REGION(0x4ff80000, 0x4ffa0000, l2_cache_256k_compat);
#else
// rev v3: usable SRAM starts 0x4ff40000 under a 256 KB cache, 0x4ff20000 under 128 KB.
SOC_RESERVE_MEMORY_REGION(0x4ff20000, 0x4ff40000, l2_cache_256k_compat);
#endif
// Give the reserved window back to the cache, at beta.1's geometry. The libs'
// startup (cache_hal_init) sets 128 KB / 8 ways / 64 B lines; the Home graph
// drew ~880 frames/min there, ~1,000 at 256 KB / 64 B, and ~1,200 at 256 KB /
// 128 B (beta.1; re-measured 2026-10-21 on a rev v3 AND a rev v1.x P4-5:
// 1,200 and 1,060, 34.7 and 36.9 ms/frame, scales streaming). The window is
// reserved above, so this costs no usable RAM. Same ROM call and order as the
// startup, plus a write-back first (PSRAM may already hold dirty lines).
// 128 B lines are safe with libs built for 64 B: the only compile-time user
// of the line size in IDF 5.5.1 is this very startup mode-set; esp_cache_msync,
// the DMA-alignment helpers, cache-aligned heap allocs and the DPI driver all
// read the live register (cache_hal_get_cache_line_size -> ROM), and a
// memory-to-cache sync on a range the new size doesn't cover is REFUSED, not
// rounded. The L2 only fronts PSRAM/flash: internal-SRAM DMA (the hosted
// radio's SDIO pool) sits behind the L1 D-cache, whose line stays 64 B. The
// Arduino core itself shipped 256 KB / 128 B on every P4 build from
// lib-builder #350 (2026-02) to #395 (2026-09), the latter a cleanup that
// dropped the ESP-SR override, not a fix. Runs at the CORE init stage: core 0
// only, the other core still parked. Both silicon generations: every unit
// runs the pinned 256 KB-cache bootloader (tools/pio_flash_layout.py), so the
// window was cache from power-on and nothing (the startup stack included)
// can live in it.
// THE SWITCH MUST NOT EXECUTE FROM FLASH. Between the geometry write and
// the invalidate the L2 holds lines filled under the old geometry, so every
// flash fetch in that window returns garbage — a jump into nowhere, a store
// to a wild address, or a bus hang, depending on what happened to be
// cached: the 2026-10-26 X-8 boot loop (CHIP_LP_WDT_RESET with nothing
// printed) that one build of the same source produced and the next did
// not. IRAM_ATTR on the function was not enough: GCC inlined it into the
// init-table entry, which is ordinary flash code. noinline keeps it a real
// IRAM function; log_l2_cache_mode() checks the address at boot. Everything
// it calls is ROM or an IRAM patch (Cache_WriteBack_All is the libs' IRAM
// patch), and the sequence is the libs' own startup order (cache enabled,
// mode registers written, then invalidate) — ROM's Cache_Set_L2_Cache_Mode
// is two register writes and ignores the ways argument (the enum is passed
// for form's sake; the literal 8 it shipped with was never looked at). Do
// NOT suspend the L2 around the write: with the suspend bit set the size
// write itself never returns (bench, 2026-10-26).
static void IRAM_ATTR __attribute__((noinline)) l2_cache_256k() {
  portDISABLE_INTERRUPTS();
  Cache_WriteBack_All(CACHE_MAP_L1_DCACHE | CACHE_MAP_L2_CACHE);
  Cache_Set_L2_Cache_Mode(CACHE_SIZE_256K, CACHE_8WAYS_ASSOC, CACHE_LINE_SIZE_128B);
  Cache_Invalidate_All(CACHE_MAP_L2_CACHE);
  portENABLE_INTERRUPTS();
}

ESP_SYSTEM_INIT_FN(l2_cache_256k_init, CORE, BIT(0), 999) {
  l2_cache_256k();
  return ESP_OK;
}
#endif

namespace platform {
void log_l2_cache_mode() {
#if CONFIG_IDF_TARGET_ESP32P4 && CONFIG_CACHE_L2_CACHE_SIZE < 0x40000
  struct cache_mode m = {};
  m.cache_type = CACHE_L2_CACHE;
  Cache_Get_Mode(&m);
  const uintptr_t fn = reinterpret_cast<uintptr_t>(&l2_cache_256k);
  core::logf("l2: %lu KB, %u ways, %u B lines (line reg %lu); switch at %p (%s)\n",
             static_cast<unsigned long>(m.cache_size / 1024), m.cache_ways,
             m.cache_line_size, static_cast<unsigned long>(Cache_Get_L2_Cache_Line_Size()),
             reinterpret_cast<void*>(fn), fn >= 0x4ff00000u && fn < 0x4ffc0000u ? "IRAM" : "NOT IRAM");
#endif
}
}  // namespace platform
