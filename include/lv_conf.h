// LVGL configuration shared by the device and host-simulator builds.
//
// Both builds put `include/` on the compiler's search path and define
// LV_CONF_INCLUDE_SIMPLE, so LVGL picks up this file. Anything not set here
// falls back to LVGL's documented defaults (see lv_conf_internal.h), so we only
// override what we actually care about.

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

// 16-bit RGB565 — matches the Waveshare RGB panel, so the simulator's PNGs show
// the same color fidelity the hardware will.
#define LV_COLOR_DEPTH 16

// LVGL's built-in allocator for widget objects (the draw/framebuffers are
// allocated separately by each display backend, not from this pool). IMPORTANT:
// LVGL's TLSF allocator spins forever in lv_realloc when this pool is exhausted
// (instead of returning NULL), so an under-sized pool shows up as a hard hang
// while building a busy screen, not a graceful failure. 64 KB left the device
// idling right at the ceiling (a scan with several results could tip it over),
// so we run 128 KB on-device. The host sim overrides this much higher via
// build_flags (-D LV_MEM_SIZE=...) since it has the RAM and builds every screen.
#ifndef LV_MEM_SIZE
#define LV_MEM_SIZE (128 * 1024)
#endif

// Quiet by default; flip to 1 with LV_LOG_LEVEL while debugging.
#define LV_USE_LOG 0

// Fonts used by the UI. Enable more sizes here as the design grows.
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#endif  // LV_CONF_H
