#include "platform_host/png_display.h"

#include <chrono>
#include <cstdlib>

#include <lvgl.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "vendor/stb_image_write.h"

namespace {

// LVGL needs a millisecond tick source. On the device this is driven by a
// hardware timer; on the host we just read a monotonic clock.
uint32_t host_tick_ms() {
  using namespace std::chrono;
  static const auto start = steady_clock::now();
  return static_cast<uint32_t>(
      duration_cast<milliseconds>(steady_clock::now() - start).count());
}

// LVGL hands us a finished frame here. In LV_DISPLAY_RENDER_MODE_FULL the area
// always covers the whole screen and px_map is the entire RGB565 framebuffer.
void flush_cb(lv_display_t* disp, const lv_area_t* /*area*/, uint8_t* px_map) {
  auto* self = static_cast<host::PngDisplay*>(lv_display_get_user_data(disp));
  const int32_t w = lv_display_get_horizontal_resolution(disp);
  const int32_t h = lv_display_get_vertical_resolution(disp);
  self->absorb_frame(px_map, w, h);
  lv_display_flush_ready(disp);
}

bool g_lvgl_inited = false;

}  // namespace

namespace host {

PngDisplay::PngDisplay(int width, int height)
    : width_(width), height_(height), display_(nullptr) {
  if (!g_lvgl_inited) {
    lv_init();
    lv_tick_set_cb(host_tick_ms);
    g_lvgl_inited = true;
  }

  render_buf_.resize(static_cast<size_t>(width) * height * 2);  // RGB565
  rgb888_.assign(static_cast<size_t>(width) * height * 3, 0);

  lv_display_t* disp = lv_display_create(width, height);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(disp, render_buf_.data(), nullptr, render_buf_.size(),
                         LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(disp, flush_cb);
  lv_display_set_user_data(disp, this);
  display_ = disp;
}

PngDisplay::~PngDisplay() {
  if (display_) lv_display_delete(static_cast<lv_display_t*>(display_));
}

void PngDisplay::render_frame() {
  lv_refr_now(static_cast<lv_display_t*>(display_));
}

void PngDisplay::absorb_frame(const uint8_t* rgb565, int width, int height) {
  const auto* src = reinterpret_cast<const uint16_t*>(rgb565);
  const int count = width * height;
  for (int i = 0; i < count; ++i) {
    const uint16_t p = src[i];
    const uint8_t r5 = (p >> 11) & 0x1F;
    const uint8_t g6 = (p >> 5) & 0x3F;
    const uint8_t b5 = p & 0x1F;
    // Expand 5/6-bit channels to 8-bit with rounding.
    rgb888_[i * 3 + 0] = (r5 * 255 + 15) / 31;
    rgb888_[i * 3 + 1] = (g6 * 255 + 31) / 63;
    rgb888_[i * 3 + 2] = (b5 * 255 + 15) / 31;
  }
}

bool PngDisplay::save_png(const char* path) const {
  return stbi_write_png(path, width_, height_, 3, rgb888_.data(),
                        width_ * 3) != 0;
}

}  // namespace host
