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
#include <sdkconfig.h>

#if CONFIG_IDF_TARGET_ESP32P4 && CONFIG_CACHE_L2_CACHE_SIZE < 0x40000
#include <heap_memory_layout.h>

#if CONFIG_ESP32P4_SELECTS_REV_LESS_V3
// rev v1.x: heap region ends 0x4ff80000 under a 256 KB cache, 0x4ffa0000 under 128 KB.
SOC_RESERVE_MEMORY_REGION(0x4ff80000, 0x4ffa0000, l2_cache_256k_compat);
#else
// rev v3: usable SRAM starts 0x4ff40000 under a 256 KB cache, 0x4ff20000 under 128 KB.
SOC_RESERVE_MEMORY_REGION(0x4ff20000, 0x4ff40000, l2_cache_256k_compat);
#endif
#endif
