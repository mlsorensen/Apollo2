#pragma once

#include <cstdint>

// Capacitive touch (CST816 on the 2-inch, GT911 elsewhere) -> LVGL pointer
// input device. Polled over I2C in the LVGL read callback (no INT line is
// used). Call begin() once after the display is up (it needs LVGL already
// initialized).
//
// Sibling of the display backend; like everything in platform_esp32 it reads
// its pins from board_config.h and the UI never sees it.

namespace platform {

class Touch {
 public:
  // Initialize I2C + the controller and register the LVGL input device.
  // screen_w/h are the on-screen (rotated) dimensions, used for the
  // coordinate mapping. Returns false if the controller doesn't ACK.
  bool begin(int screen_w, int screen_h);
};

// Controller health for the web API (/api/summary, /api/touch). The read
// callback counts every poll, every I2C failure (NAK or timeout) and the
// current failure streak, and keeps the last raw status byte and the tick of
// the last press. Added 2026-10-26 after an X-8 sat under its screensaver all
// night ignoring the glass with nothing in the log: a silent controller and
// an untouched one look identical to the driver, so this is what tells them
// apart REMOTELY, before a reset wipes the state. Plain fields written on the
// LVGL task and read from the web task — a torn read is a stale number.
struct TouchHealth {
  uint8_t addr = 0;             // resolved I2C address, 0 = never ACKed
  bool boot_ack = false;        // begin() found the controller
  uint32_t reads = 0;           // polls
  uint32_t errors = 0;          // polls that failed on the bus
  uint32_t streak = 0;          // consecutive failed polls right now
  uint32_t longest_streak = 0;  // worst streak this boot
  uint8_t last_status = 0;      // last raw status byte (GT911 0x814E)
  uint32_t last_press_ms = 0;   // lv_tick of the last pressed report, 0 = none
  uint32_t last_error_ms = 0;   // lv_tick of the last failed poll, 0 = none
};
TouchHealth touch_health();

// A deeper look at the controller, run ON THE TOUCH-READ TASK (LVGL's timer)
// so it never interleaves with a poll's write-pointer/read pair: called from
// that task it runs inline, from any other it queues the request and waits
// up to wait_ms for the next tick (done=false = that tick never came, which
// is a finding in itself). reset=true first pulses the controller's reset
// line (boards with a native TP_RST; the others report reset_supported=false
// and skip it) and re-probes both GT911 addresses.
struct TouchProbe {
  bool done = false;
  bool ack = false;             // controller ACKed its address
  char product[5] = {};         // GT9xx product id ("911", "9271") when alive
  uint8_t cfg_version = 0;      // GT911 0x8047
  uint8_t status = 0;           // GT911 0x814E, raw
  int int_level = -1;           // INT pin level, -1 = not wired
  bool reset_supported = false;
  bool reset_done = false;
  uint8_t addr = 0;             // address after the (re)probe
};
TouchProbe touch_probe(bool reset, uint32_t wait_ms);

}  // namespace platform
