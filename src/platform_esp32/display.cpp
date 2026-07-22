#include "platform_esp32/display.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include "platform_esp32/board_config.h"
#if defined(BOARD_DISPLAY_DSI)
#include <cstring>

#include <esp_cache.h>
#include <esp_lcd_mipi_dsi.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_ldo_regulator.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif
#if defined(BOARD_HAS_IO_EXTENSION)
#include <Wire.h>

#include "platform_esp32/io_extension.h"
#endif

// A single panel exists per device, and LVGL's flush callback is a plain
// function pointer, so the Arduino_GFX objects live here at file scope rather
// than as members. Display just orchestrates their setup. The panel backend
// (SPI ST7789 vs RGB parallel vs MIPI-DSI) is selected by the board's feature
// macros.
namespace {

Arduino_GFX* g_gfx = nullptr;
lv_color_t* g_draw_buf = nullptr;

// Number of screen rows LVGL renders per flush in partial mode. Larger = fewer
// bands/flushes per refresh (the flow graph's ~200px plot renders in ~1 band).
// PSRAM on RGB boards, so the size is cheap. (Probe: was 40.)
//
// HISTORY: an INTERNAL 32-line variant (~51KB, ~2.5x render speedup for the
// settings scroll) lived here briefly — and consumed the exact chunk of
// internal heap WiFi needs to start: with it + NimBLE up, free internal RAM
// bottomed at ~9KB and esp_wifi_init died with ESP_ERR_NO_MEM on every
// attempt. Reverted by choice (working WiFi over scroll fps). Don't bring it
// back while WiFi + BLE are expected to coexist; the running graphs never
// needed it (scope sweep / shot plot invalidate a few columns per frame, so
// their PSRAM blend cost is negligible).
constexpr int kBufferLines = 200;    // PSRAM chunk (RGB/DSI) / SPI chunk

uint32_t tick_cb() { return millis(); }

#if defined(BOARD_DISPLAY_DSI) && defined(BOARD_DSI_PANEL_ST7701)
// ST7701 panel init sequence, transcribed verbatim from Waveshare's BSP
// (esp32_p4_wifi6_touch_lcd_4_3.c, vendor_specific_init_default). Sent over the
// DSI DCS channel by Arduino_DSI_Display before video mode starts. The final
// pair is sleep-out (0x11, 120 ms) + display-on (0x29).
const lcd_init_cmd_t kDsiPanelInit[] = {
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x13}, 5, 0},
    {0xEF, (uint8_t[]){0x08}, 1, 0},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x10}, 5, 0},
    {0xC0, (uint8_t[]){0x63, 0x00}, 2, 0},
    {0xC1, (uint8_t[]){0x0D, 0x02}, 2, 0},
    {0xC2, (uint8_t[]){0x17, 0x08}, 2, 0},
    {0xCC, (uint8_t[]){0x10}, 1, 0},
    {0xB0, (uint8_t[]){0x40, 0xC9, 0x94, 0x0E, 0x10, 0x05, 0x0B, 0x09, 0x08,
                       0x26, 0x04, 0x52, 0x10, 0x69, 0x6B, 0x69}, 16, 0},
    {0xB1, (uint8_t[]){0x40, 0xD2, 0x98, 0x0C, 0x92, 0x07, 0x09, 0x08, 0x07,
                       0x25, 0x02, 0x0E, 0x0C, 0x6E, 0x78, 0x55}, 16, 0},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x11}, 5, 0},
    {0xB0, (uint8_t[]){0x5D}, 1, 0},
    {0xB1, (uint8_t[]){0x4E}, 1, 0},
    {0xB2, (uint8_t[]){0x87}, 1, 0},
    {0xB3, (uint8_t[]){0x80}, 1, 0},
    {0xB5, (uint8_t[]){0x4E}, 1, 0},
    {0xB7, (uint8_t[]){0x85}, 1, 0},
    {0xB8, (uint8_t[]){0x21}, 1, 0},
    {0xB9, (uint8_t[]){0x10, 0x1F}, 2, 0},
    {0xBB, (uint8_t[]){0x03}, 1, 0},
    {0xBC, (uint8_t[]){0x00}, 1, 0},
    {0xC1, (uint8_t[]){0x78}, 1, 0},
    {0xC2, (uint8_t[]){0x78}, 1, 0},
    {0xD0, (uint8_t[]){0x88}, 1, 0},
    {0xE0, (uint8_t[]){0x00, 0x3A, 0x02}, 3, 0},
    {0xE1, (uint8_t[]){0x04, 0xA0, 0x00, 0xA0, 0x05, 0xA0, 0x00, 0xA0, 0x00,
                       0x40, 0x40}, 11, 0},
    {0xE2, (uint8_t[]){0x30, 0x00, 0x40, 0x40, 0x32, 0xA0, 0x00, 0xA0, 0x00,
                       0xA0, 0x00, 0xA0, 0x00}, 13, 0},
    {0xE3, (uint8_t[]){0x00, 0x00, 0x33, 0x33}, 4, 0},
    {0xE4, (uint8_t[]){0x44, 0x44}, 2, 0},
    {0xE5, (uint8_t[]){0x09, 0x2E, 0xA0, 0xA0, 0x0B, 0x30, 0xA0, 0xA0, 0x05,
                       0x2A, 0xA0, 0xA0, 0x07, 0x2C, 0xA0, 0xA0}, 16, 0},
    {0xE6, (uint8_t[]){0x00, 0x00, 0x33, 0x33}, 4, 0},
    {0xE7, (uint8_t[]){0x44, 0x44}, 2, 0},
    {0xE8, (uint8_t[]){0x08, 0x2D, 0xA0, 0xA0, 0x0A, 0x2F, 0xA0, 0xA0, 0x04,
                       0x29, 0xA0, 0xA0, 0x06, 0x2B, 0xA0, 0xA0}, 16, 0},
    {0xEB, (uint8_t[]){0x00, 0x00, 0x4E, 0x4E, 0x00, 0x00, 0x00}, 7, 0},
    {0xEC, (uint8_t[]){0x08, 0x01}, 2, 0},
    {0xED, (uint8_t[]){0xB0, 0x2B, 0x98, 0xA4, 0x56, 0x7F, 0xFF, 0xFF, 0xFF,
                       0xFF, 0xF7, 0x65, 0x4A, 0x89, 0xB2, 0x0B}, 16, 0},
    {0xEF, (uint8_t[]){0x08, 0x08, 0x08, 0x45, 0x3F, 0x54}, 6, 0},
    {0xFF, (uint8_t[]){0x77, 0x01, 0x00, 0x00, 0x00}, 5, 0},
    {0x11, (uint8_t[]){0x00}, 0, 120},
    {0x29, (uint8_t[]){0x00}, 0, 0},
};
#elif defined(BOARD_DISPLAY_DSI) && defined(BOARD_DSI_PANEL_HX8394)
// HX8394 panel init (the 5" 720x1280): the esp_lcd_hx8394 driver's sequence,
// flattened. The driver first sends sleep-out + MADCTL 0x00 (RGB order) +
// COLMOD 0x55 (RGB565) + the 2-lane MIPI select (0xBA 0x61), then its vendor
// table (vendor_specific_init_code_default, transcribed verbatim below), which
// itself re-issues sleep-out (200 ms) and ends with display-on (0x29, 80 ms).
const lcd_init_cmd_t kDsiPanelInit[] = {
    {0x11, (uint8_t[]){0x00}, 0, 120},   // SLPOUT
    {0x36, (uint8_t[]){0x00}, 1, 0},     // MADCTL: RGB order
    {0x3A, (uint8_t[]){0x55}, 1, 0},     // COLMOD: RGB565
    {0xBA, (uint8_t[]){0x61}, 1, 0},     // SETMIPI: 2 data lanes
    // --- vendor table ---
    {0xB9, (uint8_t[]){0xFF, 0x83, 0x94}, 3, 0},  // SETEXTC: unlock
    {0xB1, (uint8_t[]){0x48, 0x0A, 0x6A, 0x09, 0x33, 0x54, 0x71, 0x71, 0x2E,
                       0x45}, 10, 0},
    {0xBA, (uint8_t[]){0x61, 0x03, 0x68, 0x6B, 0xB2, 0xC0}, 6, 0},
    {0xB2, (uint8_t[]){0x00, 0x80, 0x64, 0x0C, 0x06, 0x2F}, 6, 0},
    {0xB4, (uint8_t[]){0x1C, 0x78, 0x1C, 0x78, 0x1C, 0x78, 0x01, 0x0C, 0x86,
                       0x75, 0x00, 0x3F, 0x1C, 0x78, 0x1C, 0x78, 0x1C, 0x78,
                       0x01, 0x0C, 0x86}, 21, 0},
    {0xD3, (uint8_t[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x08, 0x32,
                       0x10, 0x05, 0x00, 0x05, 0x32, 0x13, 0xC1, 0x00, 0x01,
                       0x32, 0x10, 0x08, 0x00, 0x00, 0x37, 0x03, 0x07, 0x07,
                       0x37, 0x05, 0x05, 0x37, 0x0C, 0x40}, 33, 0},
    {0xD5, (uint8_t[]){0x18, 0x18, 0x18, 0x18, 0x22, 0x23, 0x20, 0x21, 0x04,
                       0x05, 0x06, 0x07, 0x00, 0x01, 0x02, 0x03, 0x18, 0x18,
                       0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
                       0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
                       0x18, 0x18, 0x18, 0x18, 0x19, 0x19, 0x19, 0x19}, 44, 0},
    {0xD6, (uint8_t[]){0x18, 0x18, 0x19, 0x19, 0x21, 0x20, 0x23, 0x22, 0x03,
                       0x02, 0x01, 0x00, 0x07, 0x06, 0x05, 0x04, 0x18, 0x18,
                       0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
                       0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
                       0x18, 0x18, 0x18, 0x18, 0x19, 0x19, 0x18, 0x18}, 44, 0},
    {0xE0, (uint8_t[]){0x07, 0x08, 0x09, 0x0D, 0x10, 0x14, 0x16, 0x13, 0x24,
                       0x36, 0x48, 0x4A, 0x58, 0x6F, 0x76, 0x80, 0x97, 0xA5,
                       0xA8, 0xB5, 0xC6, 0x62, 0x63, 0x68, 0x6F, 0x72, 0x78,
                       0x7F, 0x7F, 0x00, 0x02, 0x08, 0x0D, 0x0C, 0x0E, 0x0F,
                       0x10, 0x24, 0x36, 0x48, 0x4A, 0x58, 0x6F, 0x78, 0x82,
                       0x99, 0xA4, 0xA0, 0xB1, 0xC0, 0x5E, 0x5E, 0x64, 0x6B,
                       0x6C, 0x73, 0x7F, 0x7F}, 58, 0},
    {0xCC, (uint8_t[]){0x0B}, 1, 0},
    {0xC0, (uint8_t[]){0x1F, 0x73}, 2, 0},
    {0xB6, (uint8_t[]){0x6B, 0x6B}, 2, 0},
    {0xD4, (uint8_t[]){0x02}, 1, 0},
    {0xBD, (uint8_t[]){0x01}, 1, 0},
    {0xB1, (uint8_t[]){0x00}, 1, 0},
    {0xBD, (uint8_t[]){0x00}, 1, 0},
    {0xBF, (uint8_t[]){0x40, 0x81, 0x50, 0x00, 0x1A, 0xFC, 0x01}, 7, 0},
    {0x3A, (uint8_t[]){0x50}, 1, 0},
    {0x11, (uint8_t[]){0x00}, 0, 200},
    {0xB2, (uint8_t[]){0x00, 0x80, 0x64, 0x0C, 0x06, 0x2F, 0x00, 0x00, 0x00,
                       0x00, 0xC0, 0x18}, 12, 0},
    {0x29, (uint8_t[]){0x00}, 0, 80},
};
#elif defined(BOARD_DISPLAY_DSI)
#error "DSI board without a BOARD_DSI_PANEL_* controller macro (see board_config.h)."
#endif

#if defined(BOARD_DISPLAY_DSI)
// --- Double-buffered DPI scan-out ---------------------------------------
// The DPI panel continuously scans a PSRAM framebuffer; blitting LVGL strips
// into the live buffer mid-scan makes moving content (the flow graph) visibly
// blink. So: two driver-owned framebuffers. LVGL stays in PARTIAL mode and we
// rotate each strip into the BACK buffer; when the frame's last strip lands we
// present it (zero-copy scan-out switch, latched by the driver at the next
// frame boundary), wait one refresh event so the old front is off-glass, then
// copy the frame's dirty rows front->back so the back buffer is current again.
// This deliberately avoids LVGL's direct-mode dirty-sync (see the S3 4.3B
// post-mortem below) — the bookkeeping here is ours and total.
esp_lcd_panel_handle_t g_dpi_panel = nullptr;
uint16_t* g_dsi_fb[2] = {nullptr, nullptr};
int g_dsi_back = 1;   // driver scans fb0 after init; we render into fb1 first
// Dirty portrait-row union of the in-flight frame. Portrait row == landscape x.
int g_dirty_r1 = 1 << 30, g_dirty_r2 = -1;
SemaphoreHandle_t g_dsi_refresh_sem = nullptr;
// Deferred back-buffer sync: after presenting a frame we do NOT block for the
// scan-out flip. The wait + dirty-row copy happen lazily at the start of the
// NEXT frame — by then the frame boundary has almost always passed, so the
// semaphore take is free and the copy overlaps otherwise-idle time instead of
// sitting in the presented frame's flush path (measured: ~18ms -> ~2ms).
bool g_sync_pending = false;
int g_sync_r1 = 0, g_sync_r2 = -1;

void dsi_sync_back_buffer() {
  if (!g_sync_pending) return;
  // The flip is latched at the frame boundary; make sure one has passed so the
  // buffer we're about to write is genuinely off-glass.
  xSemaphoreTake(g_dsi_refresh_sem, pdMS_TO_TICKS(50));
  const size_t off = static_cast<size_t>(g_sync_r1) * board::kLcdNativeW;
  const size_t bytes = static_cast<size_t>(g_sync_r2 - g_sync_r1 + 1) *
                       board::kLcdNativeW * sizeof(uint16_t);
  std::memcpy(g_dsi_fb[g_dsi_back] + off, g_dsi_fb[g_dsi_back ^ 1] + off, bytes);
  esp_cache_msync(g_dsi_fb[g_dsi_back] + off, bytes,
                  ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
  g_sync_pending = false;
}

bool dsi_refresh_done_cb(esp_lcd_panel_handle_t, esp_lcd_dpi_panel_event_data_t*,
                         void*) {
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(g_dsi_refresh_sem, &woken);
  return woken == pdTRUE;
}

// LVGL flush for the DSI path. Rotates the landscape strip 90° into the back
// framebuffer using the same mapping as Arduino_GFX rotation=1 (portrait row =
// landscape x; portrait col = native_w-1 - landscape y), so orientation and the
// touch calibration are unchanged.
void dsi_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
  constexpr int fbw = board::kLcdNativeW;  // portrait width  (480)
  constexpr int fbh = board::kLcdNativeH;  // portrait height (800)
  const int w = lv_area_get_width(area);
  const int h = lv_area_get_height(area);
  const uint16_t* src = reinterpret_cast<uint16_t*>(px_map);
  // Before the first write into the back buffer, finish the deferred sync from
  // the previously presented frame (usually free — see dsi_sync_back_buffer).
  dsi_sync_back_buffer();
  uint16_t* fb = g_dsi_fb[g_dsi_back];
  for (int j = 0; j < h; ++j) {
    uint16_t* p = fb + static_cast<size_t>(area->x1) * fbw + (fbw - 1 - (area->y1 + j));
    const uint16_t* s = src + static_cast<size_t>(j) * w;
    for (int i = 0; i < w; ++i) {
      *p = s[i];
      p += fbw;
    }
  }
  if (area->x1 < g_dirty_r1) g_dirty_r1 = area->x1;
  if (area->x2 > g_dirty_r2) g_dirty_r2 = area->x2;

  if (lv_display_flush_is_last(disp) && g_dirty_r2 >= g_dirty_r1) {
    const int r1 = g_dirty_r1, r2 = g_dirty_r2;
    // Present: draw_bitmap with a pointer inside the back framebuffer does no
    // copy — the driver writes the touched rows back to PSRAM and switches
    // scan-out to this buffer at the next frame boundary.
    xSemaphoreTake(g_dsi_refresh_sem, 0);  // drain a stale refresh event
    esp_lcd_panel_draw_bitmap(g_dpi_panel, 0, r1, fbw, r2 + 1,
                              fb + static_cast<size_t>(r1) * fbw);
    // Don't wait here: queue the wait + dirty-row sync for the start of the
    // next frame, when the flip has long since latched.
    g_dsi_back ^= 1;
    g_sync_r1 = r1;
    g_sync_r2 = r2;
    g_sync_pending = true;
    g_dirty_r1 = 1 << 30;
    g_dirty_r2 = -1;
  }
  lv_display_flush_ready(disp);
}
#endif  // BOARD_DISPLAY_DSI

void flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
  const uint32_t w = lv_area_get_width(area);
  const uint32_t h = lv_area_get_height(area);
  // Arduino_GFX's draw16bitRGBBitmap takes native-order RGB565 (works for both
  // the SPI and RGB backends); no lv_draw_sw_rgb565_swap needed.
  g_gfx->draw16bitRGBBitmap(area->x1, area->y1, reinterpret_cast<uint16_t*>(px_map),
                            w, h);
  lv_display_flush_ready(disp);
}

}  // namespace

namespace platform {

bool Display::begin() {
#if defined(BOARD_DISPLAY_DSI)
  // MIPI-DSI panel (the P4 4.3"), direct esp_lcd — NOT Arduino_GFX — so the
  // DPI layer can own TWO framebuffers (dsi_flush_cb above presents tear-free
  // on the frame boundary). Sequence per the IDF DSI examples & Waveshare BSP:
  // PHY LDO on -> DSI bus -> DBI channel for the ST7701 DCS init -> DPI panel.
  esp_ldo_channel_handle_t ldo = nullptr;
  esp_ldo_channel_config_t ldo_cfg = {};
  ldo_cfg.chan_id = 3;  // LDO_VO3 feeds VDD_MIPI_DPHY on this board
  ldo_cfg.voltage_mv = 2500;
  if (esp_ldo_acquire_channel(&ldo_cfg, &ldo) != ESP_OK) {
    Serial.println("DSI: MIPI PHY LDO acquire FAILED");
    return false;
  }

  esp_lcd_dsi_bus_config_t bus_cfg = {};
  bus_cfg.bus_id = 0;
  bus_cfg.num_data_lanes = 2;
  bus_cfg.phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT;
  bus_cfg.lane_bit_rate_mbps = board::kDsiLaneBitRateMbps;
  esp_lcd_dsi_bus_handle_t dsi_bus = nullptr;
  if (esp_lcd_new_dsi_bus(&bus_cfg, &dsi_bus) != ESP_OK) {
    Serial.println("DSI: bus create FAILED");
    return false;
  }

  // Panel reset, then the vendor init table over the DCS (DBI) channel. The
  // assert level is per-panel: the ST7701 resets LOW, the 5"'s HX8394 HIGH.
  const int rst_assert = board::kLcdRstActiveHigh ? HIGH : LOW;
  const int rst_release = board::kLcdRstActiveHigh ? LOW : HIGH;
  pinMode(board::kLcdRst, OUTPUT);
  digitalWrite(board::kLcdRst, rst_release);
  delay(10);
  digitalWrite(board::kLcdRst, rst_assert);
  delay(10);
  digitalWrite(board::kLcdRst, rst_release);
  delay(120);

  esp_lcd_dbi_io_config_t dbi_cfg = {};
  dbi_cfg.virtual_channel = 0;
  dbi_cfg.lcd_cmd_bits = 8;
  dbi_cfg.lcd_param_bits = 8;
  esp_lcd_panel_io_handle_t dbi_io = nullptr;
  if (esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_cfg, &dbi_io) != ESP_OK) {
    Serial.println("DSI: DBI io create FAILED");
    return false;
  }
  for (size_t i = 0; i < sizeof(kDsiPanelInit) / sizeof(kDsiPanelInit[0]); ++i) {
    esp_lcd_panel_io_tx_param(dbi_io, kDsiPanelInit[i].cmd, kDsiPanelInit[i].data,
                              kDsiPanelInit[i].data_bytes);
    if (kDsiPanelInit[i].delay_ms) delay(kDsiPanelInit[i].delay_ms);
  }

  esp_lcd_dpi_panel_config_t dpi_cfg = {};
  dpi_cfg.virtual_channel = 0;
  dpi_cfg.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
  dpi_cfg.dpi_clock_freq_mhz = board::kDsiDpiClockHz / 1000000;
  dpi_cfg.pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565;
  dpi_cfg.num_fbs = 2;  // double buffer: scan one, render into the other
  dpi_cfg.video_timing.h_size = board::kLcdNativeW;
  dpi_cfg.video_timing.v_size = board::kLcdNativeH;
  dpi_cfg.video_timing.hsync_pulse_width = board::kDsiHsyncPulse;
  dpi_cfg.video_timing.hsync_back_porch = board::kDsiHsyncBack;
  dpi_cfg.video_timing.hsync_front_porch = board::kDsiHsyncFront;
  dpi_cfg.video_timing.vsync_pulse_width = board::kDsiVsyncPulse;
  dpi_cfg.video_timing.vsync_back_porch = board::kDsiVsyncBack;
  dpi_cfg.video_timing.vsync_front_porch = board::kDsiVsyncFront;
  dpi_cfg.flags.use_dma2d = true;
  if (esp_lcd_new_panel_dpi(dsi_bus, &dpi_cfg, &g_dpi_panel) != ESP_OK ||
      esp_lcd_panel_init(g_dpi_panel) != ESP_OK) {
    Serial.println("DSI: DPI panel create/init FAILED (framebuffer alloc?)");
    return false;
  }
  if (esp_lcd_dpi_panel_get_frame_buffer(g_dpi_panel, 2, (void**)&g_dsi_fb[0],
                                         (void**)&g_dsi_fb[1]) != ESP_OK) {
    Serial.println("DSI: get framebuffers FAILED");
    return false;
  }
  const size_t fb_bytes = static_cast<size_t>(board::kLcdNativeW) *
                          board::kLcdNativeH * sizeof(uint16_t);
  std::memset(g_dsi_fb[0], 0, fb_bytes);
  std::memset(g_dsi_fb[1], 0, fb_bytes);
  esp_cache_msync(g_dsi_fb[0], fb_bytes,
                  ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
  esp_cache_msync(g_dsi_fb[1], fb_bytes,
                  ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);

  g_dsi_refresh_sem = xSemaphoreCreateBinary();
  esp_lcd_dpi_panel_event_callbacks_t cbs = {};
  cbs.on_refresh_done = dsi_refresh_done_cb;
  esp_lcd_dpi_panel_register_event_callbacks(g_dpi_panel, &cbs, nullptr);

  Serial.printf("DSI: panel up %dx%d, double-buffered\n", board::kLcdNativeH,
                board::kLcdNativeW);
  // Backlight after the panel shows black: boost enable high (where a board
  // has one), then PWM full. Polarity is per-board (kBacklightActiveLow): the
  // 4.3's LEDC is inverted (duty 0 = full bright), the 5"'s is normal.
  if (board::kLcdBacklightEn >= 0) {
    pinMode(board::kLcdBacklightEn, OUTPUT);
    digitalWrite(board::kLcdBacklightEn, HIGH);
  }
  ledcAttach(board::kLcdBacklight, 5000, 8);  // 5 kHz, 8-bit PWM backlight
  ledcWrite(board::kLcdBacklight, board::kBacklightActiveLow ? 0 : 255);
#elif defined(BOARD_DISPLAY_RGB)
  // RGB parallel panel (e.g. 7B). The IO-extension I2C must come up first so we
  // can release the panel reset and turn the backlight on.
  Wire.begin(board::kI2cSda, board::kI2cScl);
  Wire.setClock(400000);
  const bool io_ok = io_extension().begin(board::kIoExtAddr);
  Serial.printf("RGB: IO extension @0x%02X on I2C(SDA=%d,SCL=%d): %s\n",
                board::kIoExtAddr, board::kI2cSda, board::kI2cScl,
                io_ok ? "ACK" : "NO ACK (backlight/reset won't work!)");
  io_extension().set(board::kIoExtLcdReset, false);
  delay(20);
  io_extension().set(board::kIoExtLcdReset, true);  // release reset
  delay(50);
  io_extension().set(board::kIoExtBacklight, true);  // backlight on early

  auto* rgbpanel = new Arduino_ESP32RGBPanel(
      board::kRgbDe, board::kRgbVsync, board::kRgbHsync, board::kRgbPclk,
      board::kRgbR[0], board::kRgbR[1], board::kRgbR[2], board::kRgbR[3], board::kRgbR[4],
      board::kRgbG[0], board::kRgbG[1], board::kRgbG[2], board::kRgbG[3], board::kRgbG[4],
      board::kRgbG[5],
      board::kRgbB[0], board::kRgbB[1], board::kRgbB[2], board::kRgbB[3], board::kRgbB[4],
      /*hsync_polarity=*/0, board::kRgbHsyncFront, board::kRgbHsyncPulse, board::kRgbHsyncBack,
      /*vsync_polarity=*/0, board::kRgbVsyncFront, board::kRgbVsyncPulse, board::kRgbVsyncBack,
      board::kRgbPclkActiveNeg, board::kRgbPclkHz, /*useBigEndian=*/false,
      /*de_idle_high=*/0, /*pclk_idle_high=*/0,
      /*bounce_buffer_size_px=*/board::kRgbBouncePx);
  g_gfx = new Arduino_RGB_Display(board::kLcdNativeW, board::kLcdNativeH, rgbpanel,
                                  board::kLcdRotation, /*auto_flush=*/true);
  if (!g_gfx->begin()) {
    Serial.println("RGB: RGB panel begin() FAILED (framebuffer alloc? PSRAM?)");
    return false;
  }
  Serial.printf("RGB: RGB panel up %dx%d\n", g_gfx->width(), g_gfx->height());
  g_gfx->fillScreen(0x0000);
  io_extension().set(board::kIoExtBacklight, true);  // backlight on
#else
  // SPI ST7789 panel (e.g. 2-inch).
  auto* bus = new Arduino_ESP32SPI(board::kLcdDc, board::kLcdCs, board::kLcdSclk,
                                   board::kLcdMosi, board::kLcdMiso, FSPI);
  g_gfx = new Arduino_ST7789(bus, board::kLcdRst, board::kLcdRotation, board::kLcdIps,
                             board::kLcdNativeW, board::kLcdNativeH);
  if (!g_gfx->begin(board::kLcdSpiHz)) return false;
  g_gfx->fillScreen(0x0000);
  ledcAttach(board::kLcdBacklight, 5000, 8);  // 5 kHz, 8-bit PWM backlight
  ledcWrite(board::kLcdBacklight, 255);
#endif

  lv_init();
  lv_tick_set_cb(tick_cb);

#if defined(BOARD_DISPLAY_DSI)
  // Logical (rotated) size: the UI is landscape on a native-portrait panel.
  const int w = board::kLcdNativeH;
  const int h = board::kLcdNativeW;
#else
  const int w = g_gfx->width();
  const int h = g_gfx->height();
#endif
  size_t buf_bytes = static_cast<size_t>(w) * kBufferLines * sizeof(lv_color_t);
#if defined(BOARD_DISPLAY_RGB) || defined(BOARD_DISPLAY_DSI)
  // Draw scratch: prefer a SMALL chunk in INTERNAL RAM. LVGL's software renderer
  // does read-modify-write blending per pixel, and over the PSRAM bus that made
  // full-page redraws (settings scrolls) crawl at ~4fps / 160ms render. A
  // 40-line internal chunk (~64KB on 800px) renders at SRAM speed with more,
  // cheaper chunks. Historical NO_MEM caution: an EARLIER build starved
  // esp_wifi_init with a 200-line internal buffer; 40 lines leaves that
  // headroom. Fallback: the old 200-line PSRAM buffer (slow but always fits).
  //
  // TEARING (why we accept it): the flow graph tears slightly because this is a
  // SINGLE framebuffer. A direct-ESP-IDF esp_lcd double-framebuffer + vsync swap
  // was built and tested on the 4.3B (preserved in a git stash on
  // feat/scale-integration). It works and is genuinely tear-free, BUT this panel
  // has no RAM of its own — the framebuffer lives in PSRAM and the panel scans it
  // continuously. Keeping BOTH framebuffers consistent for the *scrolling* graph
  // forces LVGL to re-render the whole screen every frame (DIRECT mode's cheap
  // partial-sync corrupts on a moving source -> out-of-order frames; FULL mode is
  // correct but full-screen), which on this PSRAM-bound panel is ~5 fps — worse
  // than this single-FB path's ~13 fps with cosmetic tearing. Verdict: not worth
  // it here. Viable tear-free routes if revisited: scroll the graph directly
  // inside both framebuffers (cheap: memmove the strip in each), or use an SPI
  // panel that has its own GRAM and self-refreshes.
  // PSRAM on purpose — see the kBufferLines comment: the internal variant
  // starved WiFi of its init heap.
  g_draw_buf = static_cast<lv_color_t*>(heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM));
  Serial.printf("RGB: LVGL draw buffer PSRAM %s (%u bytes)\n",
                g_draw_buf ? "ok" : "FAILED", static_cast<unsigned>(buf_bytes));
#else
  // SPI pushes this buffer over the bus via DMA -> must be DMA-capable.
  g_draw_buf = static_cast<lv_color_t*>(
      heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
#endif
  if (g_draw_buf == nullptr) return false;

  lv_display_t* disp = lv_display_create(w, h);
#if defined(BOARD_DISPLAY_DSI)
  lv_display_set_flush_cb(disp, dsi_flush_cb);
#else
  lv_display_set_flush_cb(disp, flush_cb);
#endif
  lv_display_set_buffers(disp, g_draw_buf, nullptr, buf_bytes,
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  return true;
}

#if defined(BOARD_DISPLAY_DSI)
int Display::width() const { return board::kLcdNativeH; }   // landscape UI
int Display::height() const { return board::kLcdNativeW; }
#else
int Display::width() const { return g_gfx ? g_gfx->width() : 0; }
int Display::height() const { return g_gfx ? g_gfx->height() : 0; }
#endif

void Display::set_brightness(int percent) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
#if defined(BOARD_DISPLAY_RGB)
  // (DSI boards fall through to the LEDC path below with the SPI boards.)
  // Backlight dim is purely the IO-extension PWM register here. This board's PWM
  // is inverted-duty, so set_pwm writes 255-duty (100% = brightest, 0% = off).
  // The digital backlight enable is already on from begin(), so we don't re-touch
  // it per change (avoids a redundant write fighting the PWM on the same pin).
  io_extension().set_pwm(static_cast<uint8_t>(percent));
#elif defined(BOARD_DISPLAY_DSI)
  // Polarity per board_config: the 4.3's backlight PWM is ACTIVE-LOW (its BSP
  // configures LEDC with output_invert=1; verified on hardware — normal-
  // polarity duty leaves the screen black), the 5"'s is normal.
  const int duty = percent * 255 / 100;
  ledcWrite(board::kLcdBacklight, board::kBacklightActiveLow ? 255 - duty : duty);
#else
  ledcWrite(board::kLcdBacklight, percent * 255 / 100);
#endif
}

}  // namespace platform
