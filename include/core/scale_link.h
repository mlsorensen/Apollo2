#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "core/ble.h"
#include "core/provisioner.h"  // core::ScanResult
#include "core/scale.h"

// Bluetooth-scale protocol — the scale-side analogue of core::MicraLink, and
// portable the same way (core::ble::ICentral + core/system.h only).
//
// All BLE work happens inside run() — scan, connect, (re)connect — which the
// platform calls from a dedicated thread. Once connected it subscribes to the
// scale's notify characteristic; decoded weight/timer/battery frames update a
// mutex-guarded cache that snapshot() reads from the UI thread.
//
// First/only driver: the Bookoo Themis Mini (advertised name prefix "BOOKOO";
// 20-byte notify frame on char 0xFF11; tare written to 0xFF12). Other scales slot
// in later by generalising do_connect()'s characteristic discovery + the decoder.

namespace core {

class ScaleLink : public IScale {
 public:
  explicit ScaleLink(ble::ICentral& ble) : ble_(ble) {}

  // The connection loop; blocks forever — call from a dedicated thread.
  // Empty address -> idle until set.
  void run();

  // Runtime setters (thread-safe; trigger a reconnect).
  void set_address(std::string address);
  void set_name(std::string name);  // display name for the snapshot

  // Manual connect gate (a battery remote can drop the scale to save power).
  bool connect_enabled() const { return connect_enabled_.load(); }
  void set_connect_enabled(bool enabled);

  // core::IScale — thread-safe cached read + posted command.
  ScaleSnapshot snapshot() const override;
  ScaleFeatures features() const override;
  void tare() override;

  // Discovery, run on the loop thread (used by the device ScaleProvisioner).
  void request_scan();
  bool scanning() const;
  std::vector<ScanResult> scan_results() const;

  // Called from the transport's notify callback (BLE thread) with a decoded
  // frame. Flow is derived from the weight stream by the UI, so only weight is
  // published.
  void publish_sample(float weight_g, uint32_t timer_ms, int battery_pct);

 private:
  bool do_connect(const std::string& address);
  void do_scan();
  void set_connected(bool c);

  ble::ICentral& ble_;

  std::string address_;         // guarded by mutex_
  std::string name_ = "Scale";  // guarded by mutex_

  mutable std::mutex mutex_;  // guards the cached fields below
  bool connected_ = false;
  float weight_g_ = 0.0f;
  uint32_t timer_ms_ = 0;
  int battery_pct_ = 0;
  bool battery_valid_ = false;
  uint32_t seq_ = 0;  // increments per publish_sample (snapshot.seq)
  std::vector<ScanResult> scan_results_;

  std::atomic<bool> connect_enabled_{true};  // scales auto-connect when saved
  std::atomic<bool> reconnect_requested_{false};
  std::atomic<bool> scan_requested_{false};
  std::atomic<bool> scanning_{false};
  std::atomic<bool> pending_tare_{false};
};

}  // namespace core
