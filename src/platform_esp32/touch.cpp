#include "platform_esp32/touch.h"

#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>

#include <atomic>
#include <cstring>

#include "core/system.h"
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

// Health + probe state (see touch.h). 120 B of .bss all told (measured on
// the X-8 build), no heap, no task.
platform::TouchHealth g_health;
// A probe request is (sequence << 2) | mode, mode 1 = probe, 2 = reset +
// probe; the reader task completes it by publishing that sequence. One
// caller at a time (g_probe_busy); a caller that times out withdraws its
// request, so a late tick does not run a reset nobody is watching.
std::atomic<uint32_t> g_probe_req{0};
std::atomic<uint32_t> g_probe_done_seq{0};
std::atomic<bool> g_probe_busy{false};
uint32_t g_probe_seq = 0;
platform::TouchProbe g_probe;
TaskHandle_t g_reader_task = nullptr;   // the task LVGL polls us from
// A poll failure streak this long (~2 s at LVGL's 33 ms read period) is
// logged once, and its end is logged too, so the ring carries WHEN the
// controller went quiet even if nobody probes it. A bus that flaps (streak,
// one good poll, streak...) would otherwise write a pair of lines every
// couple of seconds: "not answering" lines are at least a minute apart —
// DEFERRED, not dropped, so a streak that starts inside the gap is still
// logged once the gap has passed (if it is still going).
constexpr uint32_t kStreakLogAt = 60;
constexpr uint32_t kStreakLogGapMs = 60000;
bool g_streak_logged = false;
uint32_t g_streak_log_ms = 0;

// Whether this board can pulse the controller's reset line (an IO-extension
// bit or a native GPIO; the 2-inch CST816 board has neither).
constexpr bool kTouchCanReset =
#if defined(BOARD_HAS_IO_EXTENSION)
    true;
#elif defined(BOARD_TOUCH_GT911)
    board::kTouchRst >= 0;
#else
    false;
#endif

#if defined(BOARD_TOUCH_GT911)
// GT911 16-bit register map: status at 0x814E (bit7 = data ready, low nibble =
// point count), point 1 coords at 0x8150 (x lo/hi, y lo/hi). Status must be
// cleared to 0 after each read so the controller refills it. Product id at
// 0x8140 ("911" + NUL), config version at 0x8047.
constexpr uint16_t kGtStatus = 0x814E;
constexpr uint16_t kGtPoint1 = 0x8150;
constexpr uint16_t kGtProductId = 0x8140;
constexpr uint16_t kGtConfigVersion = 0x8047;

bool gt911_probe(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

// Register read: set the pointer (STOP, not repeated-start — the GT911 wants
// it that way), then read n bytes. False on any bus error or short read.
bool gt911_read(uint16_t reg, uint8_t* out, uint8_t n) {
  Wire.beginTransmission(g_addr);
  Wire.write(static_cast<uint8_t>(reg >> 8));
  Wire.write(static_cast<uint8_t>(reg & 0xFF));
  if (Wire.endTransmission(true) != 0) return false;
  if (Wire.requestFrom(g_addr, n) != n) return false;
  for (uint8_t i = 0; i < n; ++i) out[i] = Wire.read();
  return true;
}

// The reset pulse begin() uses, shared with the probe's reset lever: ~110 ms
// of delay() on the calling task (the GT911 wants 100 ms after reset before
// it answers), which is why only an explicit ?reset=1 pays it.
void gt911_reset_pulse() {
#if defined(BOARD_HAS_IO_EXTENSION)
  // Reset the GT911 via the IO extension. (A full address-select also wiggles
  // INT with precise timing; this is the simple pulse + probe both addresses.)
  if (board::kTouchInt >= 0) pinMode(board::kTouchInt, INPUT);
  platform::io_extension().set(board::kIoExtTouchReset, false);
  delay(10);
  platform::io_extension().set(board::kIoExtTouchReset, true);
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
}

// Look for the controller on either address, `tries` times 20 ms apart.
// Only a HIT changes g_addr: a miss keeps the address we had, so the polls
// keep talking to the bus and can see the chip come back (a probe during a
// glitch must never turn a quiet controller into a forgotten one).
bool gt911_find(int tries) {
  for (int i = 0; i < tries; ++i) {
    if (gt911_probe(0x5D)) { g_addr = 0x5D; return true; }
    if (gt911_probe(0x14)) { g_addr = 0x14; return true; }
    if (i + 1 < tries) delay(20);
  }
  return false;
}

// err = the bus failed (NAK / timeout / short read); a clean "no finger" is
// not an error. The two are told apart for the health counters only. With
// no address (begin() never found the chip) nothing is attempted and
// nothing is counted — boot_ack=false already says it.
bool read_raw(int& native_x, int& native_y, bool& err) {
  err = false;
  if (g_addr == 0) return false;
  uint8_t status = 0;
  if (!gt911_read(kGtStatus, &status, 1)) { err = true; return false; }
  g_health.last_status = status;
  if (!(status & 0x80)) return false;  // no fresh data

  bool touched = false;
  if ((status & 0x0F) > 0) {
    uint8_t p[4];
    if (gt911_read(kGtPoint1, p, 4)) {
      native_x = p[0] | (p[1] << 8);
      native_y = p[2] | (p[3] << 8);
      touched = true;
    } else {
      err = true;
    }
  }
  // Clear the status register (write 0) so the next frame is reported.
  Wire.beginTransmission(g_addr);
  Wire.write(static_cast<uint8_t>(kGtStatus >> 8));
  Wire.write(static_cast<uint8_t>(kGtStatus & 0xFF));
  Wire.write(static_cast<uint8_t>(0));
  if (Wire.endTransmission() != 0) err = true;
  return touched;
}

// Runs on the reader task. Without a reset this is a handful of short I2C
// transactions: a chip that NAKs costs a few ms in all, a WEDGED bus (SDA
// held low) costs Wire's 50 ms timeout per transaction, ~250 ms — the same
// timeout every poll is already paying in that state. A reset adds the
// ~110 ms pulse plus up to 3 x 20 ms of re-find, on the operator's explicit
// request only.
void run_probe(bool reset) {
  platform::TouchProbe r;
  r.reset_supported = kTouchCanReset;
  if (reset && kTouchCanReset) {
    gt911_reset_pulse();
    const bool found = gt911_find(3);
    r.reset_done = true;
    core::logf("touch: GT911 reset by request -> %s (addr 0x%02X)\n",
               found ? "found" : "NOT found", g_addr);
  }
  r.ack = g_addr != 0 && gt911_probe(g_addr);
  if (!r.ack) r.ack = gt911_find(1);  // maybe it (re)appeared on either address
  r.addr = g_addr;
  if (r.ack) {
    uint8_t id[4] = {};
    if (gt911_read(kGtProductId, id, 4)) {
      // The id is ASCII digits ("911", "9271" on the X-8); anything else is
      // junk from a sick chip. Keep it JSON-safe: letters and digits only, '?' for the rest.
      for (int i = 0; i < 4; ++i) {
        const char c = static_cast<char>(id[i]);
        const bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
                        (c >= 'a' && c <= 'z');
        r.product[i] = c == '\0' ? '\0' : ok ? c : '?';
      }
      r.product[4] = '\0';
    }
    gt911_read(kGtConfigVersion, &r.cfg_version, 1);
    gt911_read(kGtStatus, &r.status, 1);
  }
  if (board::kTouchInt >= 0) r.int_level = digitalRead(board::kTouchInt);
  r.done = true;
  g_probe = r;
}
#else
// CST816 (per Waveshare's 2-inch demo): reg 0x02 = finger count, 0x03/0x04 =
// X hi/lo (X high nibble in 0x03), 0x05/0x06 = Y.
bool read_raw(int& native_x, int& native_y, bool& err) {
  err = false;
  Wire.beginTransmission(static_cast<uint8_t>(board::kTouchAddr));
  Wire.write(0x02);
  if (Wire.endTransmission(false) != 0) { err = true; return false; }
  const uint8_t want = 5;  // regs 0x02..0x06
  if (Wire.requestFrom(static_cast<uint8_t>(board::kTouchAddr), want) != want) {
    err = true;
    return false;
  }
  uint8_t b[5];
  for (uint8_t i = 0; i < want; ++i) b[i] = Wire.read();
  g_health.last_status = b[0];
  if ((b[0] & 0x0F) == 0) return false;  // no finger
  native_x = ((b[1] & 0x0F) << 8) | b[2];
  native_y = ((b[3] & 0x0F) << 8) | b[4];
  return true;
}

void run_probe(bool /*reset*/) {
  platform::TouchProbe r;
  Wire.beginTransmission(static_cast<uint8_t>(board::kTouchAddr));
  r.ack = Wire.endTransmission() == 0;
  r.addr = board::kTouchAddr;
  r.done = true;
  g_probe = r;
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

void note_poll(bool err) {
  const uint32_t now = lv_tick_get();
  g_health.reads++;
  if (err) {
    g_health.errors++;
    g_health.streak++;
    g_health.last_error_ms = now;
    if (g_health.streak > g_health.longest_streak) g_health.longest_streak = g_health.streak;
    if (g_health.streak >= kStreakLogAt && !g_streak_logged &&
        (g_streak_log_ms == 0 || now - g_streak_log_ms >= kStreakLogGapMs)) {
      g_streak_logged = true;
      g_streak_log_ms = now;
      core::logf("touch: controller not answering (%lu consecutive I2C failures, "
                 "last status 0x%02X)\n",
                 static_cast<unsigned long>(g_health.streak), g_health.last_status);
    }
    return;
  }
  if (g_streak_logged) {
    core::logf("touch: controller answering again after %lu failed polls\n",
               static_cast<unsigned long>(g_health.streak));
    g_streak_logged = false;
  }
  g_health.streak = 0;
}

void read_cb(lv_indev_t* /*indev*/, lv_indev_data_t* data) {
  static int last_x = 0;
  static int last_y = 0;

  if (g_reader_task == nullptr) g_reader_task = xTaskGetCurrentTaskHandle();
  // A queued probe runs here, between polls, never inside one.
  if (const uint32_t req = g_probe_req.exchange(0); req != 0) {
    run_probe((req & 3u) == 2u);
    g_probe_done_seq.store(req >> 2);
  }

  int nx, ny;
  bool err = false;
  const bool pressed = read_raw(nx, ny, err);
  if (g_addr != 0) note_poll(err);  // no address = no transaction = no count
  if (pressed) {
    int x, y;
    map_to_screen(nx, ny, x, y);
    last_x = x;
    last_y = y;
    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
    g_health.last_press_ms = lv_tick_get();

    // One line per press, for coordinate calibration -- but only the first few
    // of a boot. Calibrating swap/mirror flags on a new board takes a handful of
    // taps; logging every press for the life of the session floods the 64KB ring
    // exactly when someone is using the machine, which is when the surrounding
    // history matters most. (Measured 2026-09-09: the ring holds only ~3.3h at
    // steady state, so anything unbounded costs real diagnostic history.)
    constexpr uint32_t kMaxLoggedPresses = 10;
    static uint32_t logged_presses = 0;
    if (!g_logged_press && logged_presses < kMaxLoggedPresses) {
      if (++logged_presses == kMaxLoggedPresses)
        core::logf("touch: (further presses not logged this boot)\n");
      core::logf("touch: native(%d,%d) -> screen(%d,%d)\n", nx, ny, x, y);
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
  gt911_reset_pulse();
  acked = gt911_find(10);
  core::logf("touch: GT911 %s (addr 0x%02X)\n", acked ? "found" : "NOT found", g_addr);
#else
  // CST816: it may need a moment after power-on; retry the ACK probe briefly.
  for (int i = 0; i < 10 && !acked; ++i) {
    Wire.beginTransmission(static_cast<uint8_t>(board::kTouchAddr));
    acked = (Wire.endTransmission() == 0);
    if (!acked) delay(50);
  }
  g_addr = static_cast<uint8_t>(board::kTouchAddr);
#endif
  g_health.boot_ack = acked;  // addr is read live from g_addr (see touch_health)

  lv_indev_t* indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, read_cb);
  return acked;
}

TouchHealth touch_health() {
  TouchHealth h = g_health;
  h.addr = g_addr;
  return h;
}

TouchProbe touch_probe(bool reset, uint32_t wait_ms) {
  TouchProbe not_done;  // done=false
  not_done.reset_supported = kTouchCanReset;
  if (g_probe_busy.exchange(true)) return not_done;  // another caller is in it
  TouchProbe out;
  if (g_reader_task == nullptr || xTaskGetCurrentTaskHandle() == g_reader_task) {
    // Same task as the poll (S3 boards serve HTTP from loop()), or LVGL has
    // never polled us: run it here, nothing to interleave with.
    run_probe(reset);
    out = g_probe;
  } else {
    const uint32_t seq = ++g_probe_seq;
    g_probe_req.store((seq << 2) | (reset ? 2u : 1u));
    const uint32_t t0 = millis();
    for (;;) {
      if (g_probe_done_seq.load() == seq) { out = g_probe; break; }
      if (millis() - t0 >= wait_ms) {
        // The LVGL tick never came. Withdraw the request so it does not
        // run (and reset) later, unobserved; if the tick took it in this
        // very instant the result is simply not reported.
        g_probe_req.store(0);
        out = not_done;
        break;
      }
      delay(10);
    }
  }
  g_probe_busy.store(false);
  return out;
}

}  // namespace platform
