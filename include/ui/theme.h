#pragma once

#include <cstdint>

// Swappable UI palette. Every color the UI draws comes from one of these roles,
// so the whole look can be changed by selecting a different Palette at runtime.
// The persisted selection lives in core::IDisplaySettings; ui::theme::set_active()
// makes it current, and the role accessors below read whichever is active.
//
// Roles (the "classes of styling"):
//   bg/rail/card  - backgrounds: app, tab rail, raised panels
//   text/muted    - foreground text: primary, secondary/captions
//   accent        - active/selected/primary controls
//   scrollbar     - scrollbar thumb
//   ok/warn/alert - status: ready-on, heating-standby, fault-stop
//
// Values are 0xRRGGBB; wrap with lv_color_hex() at the call site.

namespace ui::theme {

struct Palette {
  const char* name;
  uint32_t bg, rail, card;
  uint32_t text, muted;
  uint32_t accent;
  uint32_t scrollbar;
  uint32_t ok, warn, alert;
};

int count();
const Palette& palette(int index);  // clamped to [0, count)
const char* name(int index);
void set_active(int index);         // select the current scheme
int active_index();

// Active-palette color accessors (used everywhere instead of literals).
uint32_t bg();
uint32_t rail();
uint32_t card();
uint32_t text();
uint32_t muted();
uint32_t accent();
uint32_t scrollbar();
uint32_t ok();
uint32_t warn();
uint32_t alert();

}  // namespace ui::theme
