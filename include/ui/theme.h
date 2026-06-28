#pragma once

#include <cstdint>

// Shared UI palette. Centralizing colors here keeps the look consistent as new
// tabs and screens are added, and makes a future light/dark toggle a one-file
// change. Values are 0xRRGGBB; wrap with lv_color_hex() at the call site.

namespace ui::theme {

constexpr uint32_t bg = 0x0E1216;      // app background
constexpr uint32_t rail = 0x161B21;    // left tab rail
constexpr uint32_t card = 0x1B2026;    // raised panels
constexpr uint32_t text = 0xFFFFFF;    // primary text
constexpr uint32_t muted = 0x8A949E;   // secondary text / captions
constexpr uint32_t accent = 0x2E9BE6;  // interactive / selected

constexpr uint32_t ok = 0x2ECC71;      // ready / on
constexpr uint32_t warn = 0xF1C40F;    // heating / standby
constexpr uint32_t alert = 0xE74C3C;   // fault / stop

}  // namespace ui::theme
