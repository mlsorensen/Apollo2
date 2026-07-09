#include "platform_esp32/display.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include "platform_esp32/board_config.h"
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
constexpr int kBufferLines = 200;

uint32_t tick_cb() { return millis(); }

#if defined(BOARD_DISPLAY_DSI)
// ST7701 panel init sequence, transcribed verbatim from Waveshare's BSP
// (esp32_p4_wifi6_touch_lcd_4_3.c, vendor_specific_init_default). Sent over the
// DSI DCS channel by Arduino_DSI_Display before video mode starts. The final
// pair is sleep-out (0x11, 120 ms) + display-on (0x29).
const lcd_init_cmd_t kSt7701Init[] = {
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
  // MIPI-DSI panel (e.g. the P4 4.3"). Arduino_DSI_Display owns the esp_lcd DSI
  // bus + DPI framebuffer (PSRAM) and pulses the reset GPIO itself; rotation=1
  // maps the native-portrait panel to our landscape UI (same pattern as the
  // 2-inch ST7789).
  auto* dsipanel = new Arduino_ESP32DSIPanel(
      board::kDsiHsyncPulse, board::kDsiHsyncBack, board::kDsiHsyncFront,
      board::kDsiVsyncPulse, board::kDsiVsyncBack, board::kDsiVsyncFront,
      board::kDsiDpiClockHz, board::kDsiLaneBitRateMbps);
  g_gfx = new Arduino_DSI_Display(
      board::kLcdNativeW, board::kLcdNativeH, dsipanel, board::kLcdRotation,
      /*auto_flush=*/true, board::kLcdRst, kSt7701Init,
      sizeof(kSt7701Init) / sizeof(kSt7701Init[0]));
  if (!g_gfx->begin()) {
    Serial.println("DSI: panel begin() FAILED (DSI bus / framebuffer alloc?)");
    return false;
  }
  Serial.printf("DSI: panel up %dx%d\n", g_gfx->width(), g_gfx->height());
  g_gfx->fillScreen(0x0000);
  // Backlight after the panel shows black: boost enable high, then PWM full.
  pinMode(board::kLcdBacklightEn, OUTPUT);
  digitalWrite(board::kLcdBacklightEn, HIGH);
  ledcAttach(board::kLcdBacklight, 5000, 8);  // 5 kHz, 8-bit PWM backlight
  ledcWrite(board::kLcdBacklight, 255);
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

  const int w = g_gfx->width();
  const int h = g_gfx->height();
  const size_t buf_px = static_cast<size_t>(w) * kBufferLines;
  const size_t buf_bytes = buf_px * sizeof(lv_color_t);
#if defined(BOARD_DISPLAY_RGB) || defined(BOARD_DISPLAY_DSI)
  // Keep this scratch buffer in PSRAM. An internal-RAM scratch renders faster (no
  // contention with the panel's continuous framebuffer scan), BUT the ~64-80 KB it
  // takes is exactly what the WiFi stack needs to init for the token portal
  // (internal buffer + BLE + WiFi = esp_wifi_init NO_MEM), so PSRAM it is.
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
  g_draw_buf = static_cast<lv_color_t*>(heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM));
  if (g_draw_buf == nullptr) {
    g_draw_buf = static_cast<lv_color_t*>(heap_caps_malloc(buf_bytes, MALLOC_CAP_INTERNAL));
  }
  Serial.printf("RGB: LVGL draw buffer %s (%u bytes)\n",
                g_draw_buf ? "ok" : "FAILED", static_cast<unsigned>(buf_bytes));
#else
  // SPI pushes this buffer over the bus via DMA -> must be DMA-capable.
  g_draw_buf = static_cast<lv_color_t*>(
      heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
#endif
  if (g_draw_buf == nullptr) return false;

  lv_display_t* disp = lv_display_create(w, h);
  lv_display_set_flush_cb(disp, flush_cb);
  lv_display_set_buffers(disp, g_draw_buf, nullptr, buf_bytes,
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  return true;
}

int Display::width() const { return g_gfx ? g_gfx->width() : 0; }
int Display::height() const { return g_gfx ? g_gfx->height() : 0; }

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
#else
  ledcWrite(board::kLcdBacklight, percent * 255 / 100);
#endif
}

}  // namespace platform
