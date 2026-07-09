#include "platform_esp32/touch.h"

#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>

#include "platform_esp32/board_config.h"
#if defined(BOARD_HAS_IO_EXTENSION)
#include "platform_esp32/io_extension.h"
#endif

// One touch controller exists, and LVGL's read callback is a plain function
// pointer, so state lives here at file scope. The controller (CST816 vs GT911)
// is selected by the board's feature macros.
namespace {

int g_screen_w = 0;
int g_screen_h = 0;
uint8_t g_addr = 0;           // resolved at begin() (GT911 can be 0x5D or 0x14)
bool g_logged_press = false;  // log one line per press for calibration

#if defined(BOARD_TOUCH_GT911)
// GT911 16-bit register map: status at 0x814E (bit7 = data ready, low nibble =
// point count), point 1 coords at 0x8150 (x lo/hi, y lo/hi). Status must be
// cleared to 0 after each read so the controller refills it.
constexpr uint16_t kGtStatus = 0x814E;
constexpr uint16_t kGtPoint1 = 0x8150;

bool gt911_probe(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

bool read_raw(int& native_x, int& native_y) {
  if (g_addr == 0) return false;
  Wire.beginTransmission(g_addr);
  Wire.write(static_cast<uint8_t>(kGtStatus >> 8));
  Wire.write(static_cast<uint8_t>(kGtStatus & 0xFF));
  if (Wire.endTransmission(true) != 0) return false;  // STOP, not repeated-start
  if (Wire.requestFrom(g_addr, static_cast<uint8_t>(1)) != 1) return false;
  const uint8_t status = Wire.read();
  if (!(status & 0x80)) return false;  // no fresh data

  bool touched = false;
  if ((status & 0x0F) > 0) {
    Wire.beginTransmission(g_addr);
    Wire.write(static_cast<uint8_t>(kGtPoint1 >> 8));
    Wire.write(static_cast<uint8_t>(kGtPoint1 & 0xFF));
    if (Wire.endTransmission(true) == 0 &&
        Wire.requestFrom(g_addr, static_cast<uint8_t>(4)) == 4) {
      const uint8_t xl = Wire.read(), xh = Wire.read();
      const uint8_t yl = Wire.read(), yh = Wire.read();
      native_x = xl | (xh << 8);
      native_y = yl | (yh << 8);
      touched = true;
    }
  }
  // Clear the status register (write 0) so the next frame is reported.
  Wire.beginTransmission(g_addr);
  Wire.write(static_cast<uint8_t>(kGtStatus >> 8));
  Wire.write(static_cast<uint8_t>(kGtStatus & 0xFF));
  Wire.write(static_cast<uint8_t>(0));
  Wire.endTransmission();
  return touched;
}
#else
// CST816 (per Waveshare's 2-inch demo): reg 0x02 = finger count, 0x03/0x04 =
// X hi/lo (X high nibble in 0x03), 0x05/0x06 = Y.
bool read_raw(int& native_x, int& native_y) {
  Wire.beginTransmission(static_cast<uint8_t>(board::kTouchAddr));
  Wire.write(0x02);
  if (Wire.endTransmission(false) != 0) return false;
  const uint8_t want = 5;  // regs 0x02..0x06
  if (Wire.requestFrom(static_cast<uint8_t>(board::kTouchAddr), want) != want) return false;
  uint8_t b[5];
  for (uint8_t i = 0; i < want; ++i) b[i] = Wire.read();
  if ((b[0] & 0x0F) == 0) return false;  // no finger
  native_x = ((b[1] & 0x0F) << 8) | b[2];
  native_y = ((b[3] & 0x0F) << 8) | b[4];
  return true;
}
#endif

// Map native touch coordinates onto the rotated screen. The flags live in
// board_config.h so calibration is a one-line change, not code surgery.
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

  Wire.begin(board::kTouchSda, board::kTouchScl);  // harmless if already begun
  Wire.setClock(400000);

  bool acked = false;
#if defined(BOARD_TOUCH_GT911)
#if defined(BOARD_HAS_IO_EXTENSION)
  // Reset the GT911 via the IO extension. (A full address-select also wiggles
  // INT with precise timing; this is the simple pulse + probe both addresses.)
  if (board::kTouchInt >= 0) pinMode(board::kTouchInt, INPUT);
  io_extension().set(board::kIoExtTouchReset, false);
  delay(10);
  io_extension().set(board::kIoExtTouchReset, true);
  delay(100);
#else
  // No expander (e.g. the P4 board): same reset pulse on a native GPIO.
  if (board::kTouchInt >= 0) pinMode(board::kTouchInt, INPUT);
  if (board::kTouchRst >= 0) {
    pinMode(board::kTouchRst, OUTPUT);
    digitalWrite(board::kTouchRst, LOW);
    delay(10);
    digitalWrite(board::kTouchRst, HIGH);
    delay(100);
  }
#endif
  for (int i = 0; i < 10 && !acked; ++i) {
    if (gt911_probe(0x5D)) { g_addr = 0x5D; acked = true; }
    else if (gt911_probe(0x14)) { g_addr = 0x14; acked = true; }
    else delay(20);
  }
  Serial.printf("touch: GT911 %s (addr 0x%02X)\n", acked ? "found" : "NOT found", g_addr);
#else
  // CST816: it may need a moment after power-on; retry the ACK probe briefly.
  for (int i = 0; i < 10 && !acked; ++i) {
    Wire.beginTransmission(static_cast<uint8_t>(board::kTouchAddr));
    acked = (Wire.endTransmission() == 0);
    if (!acked) delay(50);
  }
#endif

  lv_indev_t* indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, read_cb);
  return acked;
}

}  // namespace platform
