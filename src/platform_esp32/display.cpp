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
// (SPI ST7789 vs RGB parallel) is selected by the board's feature macros.
namespace {

Arduino_GFX* g_gfx = nullptr;
lv_color_t* g_draw_buf = nullptr;

// Number of screen rows LVGL renders per flush in partial mode.
constexpr int kBufferLines = 40;

uint32_t tick_cb() { return millis(); }

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
#if defined(BOARD_DISPLAY_RGB)
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
#if defined(BOARD_DISPLAY_RGB)
  // Keep the LVGL scratch buffer in INTERNAL RAM. LVGL renders into it pixel by
  // pixel (CPU), and the RGB panel's framebuffer scan already saturates PSRAM
  // bandwidth — a PSRAM scratch buffer fights that scan and makes rendering (and
  // thus input handling) crawl. It's only a few lines (~64-80 KB), which fits.
  // Fall back to PSRAM only if internal is exhausted.
  g_draw_buf = static_cast<lv_color_t*>(
      heap_caps_malloc(buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  const bool internal = g_draw_buf != nullptr;
  if (g_draw_buf == nullptr) {
    g_draw_buf = static_cast<lv_color_t*>(heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM));
  }
  Serial.printf("RGB: LVGL draw buffer %s in %s (%u bytes)\n",
                g_draw_buf ? "ok" : "FAILED", internal ? "internal" : "PSRAM",
                static_cast<unsigned>(buf_bytes));
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
