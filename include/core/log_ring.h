#pragma once

#include <cstddef>
#include <mutex>

// In-RAM ring of recent diagnostic log text, so a morning mystery can be
// diagnosed after the fact (Stats > Info > log viewer, or http://<ip>/log)
// instead of requiring a serial cable attached at the moment of failure.
//
// Lines are stamped at write time: "[HH:MM:SS] " once a plausible wall clock
// is attached, "[+123.456] " (seconds since boot) before that. The ring stores
// the final stamped text, so readers just dump bytes; the platform's serial
// echo goes through the sink callback and carries the same stamps.
//
// The buffer is provided by the platform (PSRAM on ESP32, a static array on
// the host) — core allocates nothing. Writes are mutex-guarded: logf is called
// from several tasks on-device (main loop, BLE host, web server).

namespace core {

class IClock;

class LogRing {
 public:
  // Echo sink for stamped text segments (the ESP32 wires this to Serial).
  using Sink = void (*)(const char* text, size_t len);

  // Hand the ring its storage. Before this, write() only echoes (nothing is
  // retained) — early-boot lines still reach serial.
  void init(char* buf, size_t capacity);
  bool ready() const { return buf_ != nullptr; }
  size_t capacity() const { return cap_; }

  void set_sink(Sink sink) { sink_ = sink; }
  // Enables wall-clock stamps. The clock only counts once it reports a year
  // >= kClockBaseYear (the "was ever set" sentinel — see platform Clock).
  void set_clock(const IClock* clock) { clock_ = clock; }

  // Append text (multi-line safe; a stamp is inserted at every line start).
  // `echo` false suppresses the sink — used for IDF log capture, where the
  // original vprintf still prints to the console.
  void write(const char* text, bool echo = true);

  // Copy the newest bytes into out (NUL-terminated, at most max_len bytes
  // including the NUL), starting at a line boundary when the window is
  // truncated. Returns the text length written.
  size_t snapshot_tail(char* out, size_t max_len) const;

 private:
  void make_stamp(char* out, size_t n) const;
  void push(const char* p, size_t n);  // raw ring append (caller holds mu_)

  char* buf_ = nullptr;
  size_t cap_ = 0;
  size_t head_ = 0;  // next write offset
  size_t used_ = 0;  // valid bytes (== cap_ once wrapped)
  bool at_line_start_ = true;
  Sink sink_ = nullptr;
  const IClock* clock_ = nullptr;
  mutable std::mutex mu_;
};

// The process-wide ring every platform's logf feeds.
LogRing& log_ring();

}  // namespace core
