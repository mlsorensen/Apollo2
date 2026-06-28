#include "platform_esp32/display.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include "platform_esp32/board_config.h"

// A single panel exists per device, and LVGL's flush callback is a plain
// function pointer, so the Arduino_GFX objects live here at file scope rather
// than as members. Display just orchestrates their setup.
namespace {

Arduino_DataBus* g_bus = nullptr;
Arduino_GFX* g_gfx = nullptr;
lv_color_t* g_draw_buf = nullptr;

// Number of screen rows LVGL renders per flush in partial mode. Bigger = fewer
// flush calls but more RAM; 40 lines is a common, comfortable middle.
constexpr int kBufferLines = 40;

uint32_t tick_cb() { return millis(); }

void flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
  const uint32_t w = lv_area_get_width(area);
  const uint32_t h = lv_area_get_height(area);

  // Arduino_GFX's draw16bitRGBBitmap expects native-order RGB565 and handles the
  // SPI byte order itself — no lv_draw_sw_rgb565_swap needed. (Swapping here
  // rotates the channels: red->blue, green->red, and fringes anti-aliased text.)
  g_gfx->draw16bitRGBBitmap(area->x1, area->y1, reinterpret_cast<uint16_t*>(px_map),
                            w, h);

  lv_display_flush_ready(disp);
}

}  // namespace

namespace platform {

bool Display::begin() {
  g_bus = new Arduino_ESP32SPI(board::kLcdDc, board::kLcdCs, board::kLcdSclk,
                               board::kLcdMosi, board::kLcdMiso, FSPI);
  g_gfx = new Arduino_ST7789(g_bus, board::kLcdRst, board::kLcdRotation,
                             board::kLcdIps, board::kLcdNativeW, board::kLcdNativeH);

  if (!g_gfx->begin(board::kLcdSpiHz)) return false;
  g_gfx->fillScreen(0x0000);  // RGB565 black (Arduino_GFX 1.6 renamed BLACK)

  pinMode(board::kLcdBacklight, OUTPUT);
  digitalWrite(board::kLcdBacklight, HIGH);  // full brightness; PWM dimming later

  lv_init();
  lv_tick_set_cb(tick_cb);

  const int w = g_gfx->width();
  const int h = g_gfx->height();

  const size_t buf_px = static_cast<size_t>(w) * kBufferLines;
  const size_t buf_bytes = buf_px * sizeof(lv_color_t);
  // DMA-capable internal RAM: Arduino_GFX pushes this buffer over SPI via DMA.
  g_draw_buf = static_cast<lv_color_t*>(
      heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  if (g_draw_buf == nullptr) return false;

  lv_display_t* disp = lv_display_create(w, h);
  lv_display_set_flush_cb(disp, flush_cb);
  lv_display_set_buffers(disp, g_draw_buf, nullptr, buf_bytes,
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  return true;
}

int Display::width() const { return g_gfx ? g_gfx->width() : 0; }
int Display::height() const { return g_gfx ? g_gfx->height() : 0; }

}  // namespace platform
