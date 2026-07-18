#pragma once

#include <cstdint>

// Core domain layer: a Bluetooth coffee scale and the port the UI talks to.
//
// IMPORTANT: like core::IMachine, this header stays free of LVGL, Arduino, BLE,
// and SDL. The on-device implementation is the NimBLE ScaleLink (with per-model
// drivers — Bookoo Themis first); the host fake feeds canned values so the UI
// renders on a laptop. Modelled on the goscale Scale interface
// (connect/weight-stream/tare/features), adapted to this project's
// snapshot-polling style: the UI polls ~2/s rather than subscribing to a channel.

namespace core {

// What a given scale model supports. Drives which controls the UI offers.
struct ScaleFeatures {
  bool tare;     // can be zeroed over BLE
  bool flow;     // reports a flow rate (g/s) natively
  bool timer;    // reports a built-in shot timer
  bool battery;  // reports a battery charge level
  bool beep;     // beep on/off is settable
};

// A flat, copyable snapshot of the scale's latest state (enough to draw one UI
// frame). Filled from the BLE notification stream on the device; canned on the
// host. Trivially copyable, so it crosses the BLE-task / UI-thread boundary
// safely (the `name` is a stable pointer).
struct ScaleSnapshot {
  const char* name;          // connected scale name (stable pointer), "" if none
  bool        connected;
  float       weight_g;      // current weight in grams (may be negative)
  uint32_t    timer_ms;      // built-in shot timer, ms (0 if !features.timer)
  bool        battery_valid;
  int         battery_pct;   // 0..100 when battery_valid
  uint32_t    seq;           // increments per received update — consumers use it
                             // to draw event-locked and to measure the stream
                             // rate (scales differ: ~2..20Hz)
};

// The port the UI depends on for scale data + commands. The BLE ScaleLink and
// the host fake both implement it; the UI holds only this reference, never a
// concrete transport.
class IScale {
 public:
  virtual ~IScale() = default;

  // Latest known state. Cheap and synchronous (a cached read) — implementations
  // must not block on I/O here.
  virtual ScaleSnapshot snapshot() const = 0;

  // What the connected scale model supports (all-false when nothing connected).
  virtual ScaleFeatures features() const = 0;

  // Command: zero the scale. No-op if unsupported or disconnected. May block
  // briefly on a transport write.
  virtual void tare() = 0;
};

}  // namespace core
