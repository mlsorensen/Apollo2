#include "ui/theme.h"

namespace ui::theme {

namespace {

// Cohesive dark schemes. `text` is light in all of them so it stays legible
// on both `card` and the saturated `accent` (button labels use `text`).
const Palette kPalettes[] = {
    //          name          bg        rail      card      text      muted     accent    scroll    ok        warn      alert
    {"Midnight",  0x0E1216, 0x161B21, 0x1B2026, 0xFFFFFF, 0x8A949E, 0x2E9BE6, 0x5A6470, 0x2ECC71, 0xF1C40F, 0xE74C3C},
    {"Graphite",  0x121212, 0x1C1C1C, 0x242424, 0xF5F5F5, 0x9E9E9E, 0x5C9DFF, 0x6E6E6E, 0x4CAF50, 0xFFB300, 0xEF5350},
    {"Espresso",  0x1A1310, 0x241A15, 0x2E211A, 0xF3E9DF, 0xB59C86, 0xCE7B33, 0x7A6552, 0x9CCC65, 0xE0A845, 0xD9583B},
    {"Nord",      0x2E3440, 0x3B4252, 0x434C5E, 0xECEFF4, 0x9AA3B5, 0x88C0D0, 0x6A748B, 0xA3BE8C, 0xEBCB8B, 0xBF616A},
    {"Solarized", 0x002B36, 0x073642, 0x0A4250, 0x93A1A1, 0x657B83, 0x268BD2, 0x4A6470, 0x859900, 0xB58900, 0xDC322F},
    //          name          bg        rail      card      text      muted     accent    scroll    ok        warn      alert
    {"Plum",      0x15111C, 0x1F1829, 0x281F35, 0xF2ECF8, 0x9C8FB0, 0xA663E0, 0x5C4E72, 0x5BC98C, 0xE7B84A, 0xE06A6A},
    {"Forest",    0x0E1411, 0x151E18, 0x1C2A22, 0xEAF3EC, 0x8EA897, 0x2FAE76, 0x4F6657, 0x6FD08D, 0xE3B642, 0xE05D4A},
    {"Rose",      0x17100F, 0x221614, 0x2D1C1A, 0xF6EBE8, 0xB29691, 0xE0577E, 0x6B514C, 0x66C07C, 0xE6AE45, 0xCF4636},
    // Append-only: the theme setting persists as an INDEX (NVS "theme"), so
    // reordering or removing entries silently re-themes existing devices.
    //          name          bg        rail      card      text      muted     accent    scroll    ok        warn      alert
    // Mono: greyscale chrome; only the ok/warn/alert status roles keep muted
    // color (the dots/graph encode live machine state — grey would erase it).
    {"Mono",      0x000000, 0x101010, 0x191919, 0xFFFFFF, 0x9A9A9A, 0xB9B9B9, 0x5E5E5E, 0x8FBF8F, 0xC9A94B, 0xC96A5C},
    // High contrast: pure black, brighter secondary text, vivid saturated
    // status colors, thumb bumped so it never vanishes.
    {"Contrast",  0x000000, 0x000000, 0x141414, 0xFFFFFF, 0xD0D0D0, 0x00A2FF, 0x8A8A8A, 0x00E676, 0xFFD600, 0xFF3D2E},
    // Rosso corsa accent on a dark warm-red body; giallo modena as warn.
    {"Ferrari",   0x140607, 0x1E0A0C, 0x2A0E12, 0xFFF4EC, 0xBC8F86, 0xE8112D, 0x84443F, 0x63C74D, 0xFCD116, 0xFF7043},
    // Sunset: coral accent over warm plum-brown.
    {"Sunset",    0x1A0F14, 0x241419, 0x301A20, 0xFFEDE3, 0xB58F92, 0xFF7043, 0x6E4A50, 0x66BB6A, 0xFFCA28, 0xEF5350},
    // Citrus: lime accent over deep olive (yellow-green, vs Forest's cool green).
    {"Citrus",    0x111704, 0x18210A, 0x202B10, 0xF4F8E8, 0xA3B183, 0x8FB824, 0x53613B, 0x6FD08D, 0xE3B642, 0xE05D4A},
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
