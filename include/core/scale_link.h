#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/ble.h"
#include "core/provisioner.h"  // core::ScanResult
#include "core/scale.h"
#include "core/scale_driver.h"

// Bluetooth-scale connection manager — the scale-side analogue of core::MicraLink,
// and portable the same way (core::ble::ICentral + core/system.h only).
//
// All BLE work happens inside run() — scan, connect, (re)connect — which the
// platform calls from a dedicated thread. Everything model-specific (UUIDs,
// init sequence, frame decode, keepalives, command bytes) lives behind
// core::IScaleDriver (scale_driver.h): the saved scale's name classifies the
// model, do_connect() instantiates a fresh driver per connection, and decoded
// weight/timer/battery frames land in a mutex-guarded cache that snapshot()
// reads from the UI thread. Drivers today: Bookoo Themis, Acaia (Umbra/Lunar/
// Pyxis).

namespace core {

class ScaleLink : public IScale, public IScaleSink {
 public:
  explicit ScaleLink(ble::ICentral& ble) : ble_(ble) {}

  // The connection loop; blocks forever — call from a dedicated thread.
  // Empty address -> idle until set.
  void run();

  // Runtime setters (thread-safe; trigger a reconnect).
  void set_address(std::string address);
  void set_name(std::string name);  // display name; also selects the driver

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

  // Radio arbitration with a peer link sharing the BLE host (see MicraLink's
  // twin methods): a link about to scan pauses its peer's connect attempts.
  void pause_connects(bool on);
  void set_scan_peer_pauser(std::function<void(bool)> p) { peer_pause_ = std::move(p); }

  // core::IScaleSink — called from the transport's notify callback (BLE
  // thread) by the driver. Flow is derived from the weight stream by the UI,
  // so only weight is published; on_weight bumps snapshot.seq.
  void on_weight(float grams) override;
  void on_timer(uint32_t timer_ms) override;
  void on_battery(int pct) override;

 private:
  bool do_connect(const std::string& address);
  void do_scan();
  void set_connected(bool c);

  ble::ICentral& ble_;

  std::string address_;         // guarded by mutex_
  std::string name_ = "Scale";  // guarded by mutex_

  // Model driver for the saved scale; refreshed per connection attempt.
  // Guarded by mutex_; shared_ptr so the notify callback + link loop keep a
  // replaced driver alive until they let go.
  std::shared_ptr<IScaleDriver> driver_;

  mutable std::mutex mutex_;  // guards the cached fields below
  bool connected_ = false;
  float weight_g_ = 0.0f;
  uint32_t timer_ms_ = 0;
  int battery_pct_ = 0;
  bool battery_valid_ = false;
  uint32_t seq_ = 0;  // increments per weight update (snapshot.seq)
  std::vector<ScanResult> scan_results_;

  std::atomic<bool> connect_enabled_{true};  // scales auto-connect when saved
  std::atomic<bool> reconnect_requested_{false};
  std::atomic<bool> scan_requested_{false};
  std::atomic<bool> scanning_{false};
  std::atomic<bool> pending_tare_{false};
  std::atomic<bool> connects_paused_{false};  // peer is scanning — hold off
  std::function<void(bool)> peer_pause_;      // set once before the loop
};

}  // namespace core
