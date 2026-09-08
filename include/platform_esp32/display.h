#pragma once

// On-device display backend: brings up the panel for the selected board and
// registers it with LVGL as the active display. This is the device counterpart
// of the host PngDisplay — same role (get LVGL pixels onto a screen), different
// target. The UI layer is unaware of which backend is running.
//
// Touch is a separate concern (added later) so the display can be brought up
// and validated on its own.

namespace platform {

class Display {
 public:
  // Initialize the panel + backlight + LVGL display. Returns false if the
  // panel or draw buffer could not be allocated. Call once from setup().
  bool begin();

  int width() const;   // logical width after rotation (0 before begin())
  int height() const;  // logical height after rotation

  void set_brightness(int percent);  // backlight PWM, 0..100

  // RGB boards only: restart the panel's pixel pipeline from frame zero
  // (esp_lcd_panel_init) — heals the shifted/ghosted raster that a
  // bounce-buffer underrun latches, without rebooting. ~One frame of cost,
  // visually imperceptible (HW-verified). Returns false on non-RGB boards
  // (caller falls back to a full soft reboot). verbose=false for the
  // periodic auto-resync so it doesn't spam serial.
  bool rgb_resync(bool verbose = true);
};

#if defined(BOARD_DISPLAY_DSI)
// Lightweight display for the boot-flag OTA install mode (install_mode.cpp):
// the per-board DSI panel with num_fbs=2 but no use_dma2d / async dirty-sync,
// so the internal-DMA pool stays free for the hosted-radio download. Bring the
// panel up, present full portrait frames, and blank the backlight for the
// (screen-glitching) flash-write phase. DSI boards only.
bool install_panel_begin();
void install_panel_present(const uint16_t* portrait_fb);  // native WxH RGB565
void install_panel_backlight(bool on);
#endif

}  // namespace platform
