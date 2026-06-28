#include "platform_esp32/touch.h"

#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>

#include "platform_esp32/board_config.h"

// One touch controller exists, and LVGL's read callback is a plain function
// pointer, so state lives here at file scope.
namespace {

int g_screen_w = 0;
int g_screen_h = 0;
bool g_logged_press = false;  // log one line per press for calibration

// Read the CST816 touch registers. Layout (per Waveshare's demo): reg 0x02 =
// finger count, 0x03/0x04 = X hi/lo (X high nibble in 0x03), 0x05/0x06 = Y.
// Returns false when no finger is down or the bus read fails.
bool read_raw(int& native_x, int& native_y) {
  Wire.beginTransmission(static_cast<uint8_t>(board::kTouchAddr));
  Wire.write(0x02);
  if (Wire.endTransmission(false) != 0) return false;  // repeated-start read

  const uint8_t want = 5;  // regs 0x02..0x06
  if (Wire.requestFrom(static_cast<uint8_t>(board::kTouchAddr), want) != want) {
    return false;
  }
  uint8_t b[5];
  for (uint8_t i = 0; i < want; ++i) b[i] = Wire.read();

  if ((b[0] & 0x0F) == 0) return false;  // no finger
  native_x = ((b[1] & 0x0F) << 8) | b[2];
  native_y = ((b[3] & 0x0F) << 8) | b[4];
  return true;
}

// Map native (portrait) touch coordinates onto the rotated screen. The flags
// live in board_config.h so calibration is a one-line change, not code surgery.
void map_to_screen(int native_x, int native_y, int& x, int& y) {
  x = native_x;
  y = native_y;
  if (board::kTouchSwapXY) {
    const int t = x;
    x = y;
    y = t;
  }
  if (board::kTouchMirrorX) x = (g_screen_w - 1) - x;
  if (board::kTouchMirrorY) y = (g_screen_h - 1) - y;

  if (x < 0) x = 0;
  if (x >= g_screen_w) x = g_screen_w - 1;
  if (y < 0) y = 0;
  if (y >= g_screen_h) y = g_screen_h - 1;
}

void read_cb(lv_indev_t* /*indev*/, lv_indev_data_t* data) {
  static int last_x = 0;
  static int last_y = 0;

  int nx, ny;
  if (read_raw(nx, ny)) {
    int x, y;
    map_to_screen(nx, ny, x, y);
    last_x = x;
    last_y = y;
    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;

    if (!g_logged_press) {  // one line per press, for coordinate calibration
      Serial.printf("touch: native(%d,%d) -> screen(%d,%d)\n", nx, ny, x, y);
      g_logged_press = true;
    }
  } else {
    data->point.x = last_x;  // LVGL wants the last point on release
    data->point.y = last_y;
    data->state = LV_INDEV_STATE_RELEASED;
    g_logged_press = false;
  }
}

}  // namespace

namespace platform {

bool Touch::begin(int screen_w, int screen_h) {
  g_screen_w = screen_w;
  g_screen_h = screen_h;

  Wire.begin(board::kTouchSda, board::kTouchScl);
  Wire.setClock(400000);

  // The CST816 may need a moment after power-on; retry the ACK probe briefly.
  bool acked = false;
  for (int i = 0; i < 10 && !acked; ++i) {
    Wire.beginTransmission(static_cast<uint8_t>(board::kTouchAddr));
    acked = (Wire.endTransmission() == 0);
    if (!acked) delay(50);
  }

  lv_indev_t* indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, read_cb);
  return acked;
}

}  // namespace platform
