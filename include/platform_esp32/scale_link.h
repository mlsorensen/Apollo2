#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "core/provisioner.h"  // core::ScanResult
#include "core/scale.h"

// NimBLE Bluetooth-scale transport — the scale-side analogue of MicraLink.
//
// All BLE work runs on a dedicated FreeRTOS task started by begin(): scan,
// connect, and (re)connect. Once connected it subscribes to the scale's notify
// characteristic; decoded weight/flow/timer/battery frames update a mutex-guarded
// cache that snapshot() reads from the UI thread. One NimBLE host holds this
// connection concurrently with MicraLink's (NimBLE default max-connections = 3).
//
// First/only driver: the Bookoo Themis Mini (advertised name prefix "BOOKOO";
// 20-byte notify frame on char 0xFF11; tare written to 0xFF12). Other scales slot
// in later by generalising do_connect()'s characteristic discovery + the decoder.

namespace platform {

class ScaleLink : public core::IScale {
 public:
  ScaleLink() = default;

  // Start the background task (non-blocking). Empty address -> idle until set.
  void begin(std::string address);

  // Runtime setters (thread-safe; trigger a reconnect).
  void set_address(std::string address);
  void set_name(std::string name);  // display name for the snapshot

  // Manual connect gate (a battery remote can drop the scale to save power).
  bool connect_enabled() const { return connect_enabled_.load(); }
  void set_connect_enabled(bool enabled);

  // core::IScale — thread-safe cached read + posted command.
  core::ScaleSnapshot snapshot() const override;
  core::ScaleFeatures features() const override;
  void tare() override;
  size_t drain_flow(float* out, size_t max) override;

  // Discovery, run on the BLE task (used by the device ScaleProvisioner).
  void request_scan();
  bool scanning() const;
  std::vector<core::ScanResult> scan_results() const;

  // Called from the NimBLE notify callback (BLE host task) with a decoded frame.
  void publish_sample(float weight_g, float flow_gps, uint32_t timer_ms,
                      int battery_pct);

 private:
  static void task_entry(void* arg);
  void task_loop();
  bool do_connect(const std::string& address);
  void do_scan();
  void set_connected(bool c);

  std::string address_;        // guarded by mutex_
  std::string name_ = "Scale";  // guarded by mutex_

  mutable std::mutex mutex_;   // guards the cached fields below
  bool connected_ = false;
  float weight_g_ = 0.0f;
  float flow_gps_ = 0.0f;
  uint32_t timer_ms_ = 0;
  int battery_pct_ = 0;
  bool battery_valid_ = false;
  std::vector<float> pending_flow_;  // notify samples awaiting the chart (mutex_)
  std::vector<core::ScanResult> scan_results_;

  std::atomic<bool> connect_enabled_{true};   // scales auto-connect when saved
  std::atomic<bool> reconnect_requested_{false};
  std::atomic<bool> scan_requested_{false};
  std::atomic<bool> scanning_{false};
  std::atomic<bool> pending_tare_{false};
};

}  // namespace platform
