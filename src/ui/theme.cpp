#include "ui/theme.h"

namespace ui::theme {

namespace {

// Five cohesive dark schemes. `text` is light in all of them so it stays legible
// on both `card` and the saturated `accent` (button labels use `text`).
const Palette kPalettes[] = {
    //          name          bg        rail      card      text      muted     accent    scroll    ok        warn      alert
    {"Midnight",  0x0E1216, 0x161B21, 0x1B2026, 0xFFFFFF, 0x8A949E, 0x2E9BE6, 0x5A6470, 0x2ECC71, 0xF1C40F, 0xE74C3C},
    {"Graphite",  0x121212, 0x1C1C1C, 0x242424, 0xF5F5F5, 0x9E9E9E, 0x5C9DFF, 0x6E6E6E, 0x4CAF50, 0xFFB300, 0xEF5350},
    {"Espresso",  0x1A1310, 0x241A15, 0x2E211A, 0xF3E9DF, 0xB59C86, 0xCE7B33, 0x7A6552, 0x9CCC65, 0xE0A845, 0xD9583B},
    {"Nord",      0x2E3440, 0x3B4252, 0x434C5E, 0xECEFF4, 0x9AA3B5, 0x88C0D0, 0x6A748B, 0xA3BE8C, 0xEBCB8B, 0xBF616A},
    {"Solarized", 0x002B36, 0x073642, 0x0A4250, 0x93A1A1, 0x657B83, 0x268BD2, 0x4A6470, 0x859900, 0xB58900, 0xDC322F},
};
constexpr int kCount = static_cast<int>(sizeof(kPalettes) / sizeof(kPalettes[0]));

int clamp(int i) { return i < 0 ? 0 : (i >= kCount ? kCount - 1 : i); }

int g_active = 0;

}  // namespace

int count() { return kCount; }
const Palette& palette(int index) { return kPalettes[clamp(index)]; }
const char* name(int index) { return kPalettes[clamp(index)].name; }
void set_active(int index) { g_active = clamp(index); }
int active_index() { return g_active; }

uint32_t bg() { return kPalettes[g_active].bg; }
uint32_t rail() { return kPalettes[g_active].rail; }
uint32_t card() { return kPalettes[g_active].card; }
uint32_t text() { return kPalettes[g_active].text; }
uint32_t muted() { return kPalettes[g_active].muted; }
uint32_t accent() { return kPalettes[g_active].accent; }
uint32_t scrollbar() { return kPalettes[g_active].scrollbar; }
uint32_t ok() { return kPalettes[g_active].ok; }
uint32_t warn() { return kPalettes[g_active].warn; }
uint32_t alert() { return kPalettes[g_active].alert; }

}  // namespace ui::theme
