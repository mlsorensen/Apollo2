#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/ble.h"
#include "core/provisioner.h"  // core::ScanResult
#include "core/radio_arbiter.h"
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
  // `slot` names this link to the radio arbiter (espresso or bench); every
  // listen on the radio — reconnects, the finder's scan chunks, the user's
  // Settings scan — is granted by `arb` and run on this link's own thread.
  ScaleLink(ble::ICentral& ble, RadioArbiter& arb, RadioSlot slot)
      : ble_(ble), arb_(arb), slot_(slot) {}
  RadioSlot slot() const { return slot_; }

  // The connection loop; blocks forever — call from a dedicated thread.
  // Empty address -> idle until set.
  void run();

  // Runtime setters (thread-safe; trigger a reconnect). `type` is the saved
  // scale's address type as learned at pairing time or on a previous connect
  // (kUnknown = not yet: the transport tries both and the link learns it).
  void set_address(std::string address, AddrType type = AddrType::kUnknown);
  void set_name(std::string name);  // display name; also selects the driver
  // Called (from the link thread) when a connect LEARNS the address type —
  // the first connect on a unit paired before the type was recorded, or a
  // scale whose type changed. The platform persists it next to the MAC.
  void set_addr_type_learned(std::function<void(AddrType)> cb) {
    addr_type_learned_ = std::move(cb);
  }

  // Partner-mode support for Umbra firmware 1.0.8.
  void set_partner_mode(bool enabled);
  bool partner_mode() const { return partner_mode_.load(); }
  bool partner_pairing_complete() const { return partner_paired_.load(); }
  void on_partner_pairing_complete() { partner_paired_.store(true); }
  void set_partner_connected(bool connected);
  void on_partner_weight(float grams) { on_weight(grams); }
  void on_partner_battery(int pct) { on_battery(pct); }
  void on_partner_setting(int index, int option_idx) {
    on_device_setting(index, option_idx);
  }
  void set_partner_setting_writer(std::function<void(int, int)> writer) {
    partner_setting_writer_ = std::move(writer);
  }

  // Manual connect gate (a battery remote can drop the scale to save power).
  bool connect_enabled() const { return connect_enabled_.load(); }
  void set_connect_enabled(bool enabled);

  // core::IScale — thread-safe cached read + posted command.
  ScaleSnapshot snapshot() const override;
  ScaleFeatures features() const override;
  void tare() override;
  int device_setting_count() const override;
  ScaleSettingDesc device_setting(int index) const override;
  int device_setting_value(int index) const override;
  void set_device_setting(int index, int option_idx) override;

  // Discovery, run on the loop thread (used by the device ScaleProvisioner).
  void request_scan();
  bool scanning() const;
  std::vector<ScanResult> scan_results() const;

  // core::IScaleSink — called from the transport's notify callback (BLE
  // thread) by the driver. Flow is derived from the weight stream by the UI,
  // so only weight is published; on_weight bumps snapshot.seq.
  void on_weight(float grams) override;
  void on_timer(uint32_t timer_ms) override;
  void on_battery(int pct) override;
  void on_device_setting(int index, int option_idx) override;

 private:
  bool do_connect(const std::string& address, AddrType type, uint32_t timeout_ms);
  void do_scan();
  void set_connected(bool c);
  bool link_up() const;  // the raw BLE link, before the weight-stream gate
  void sync_radio_state();  // feed the arbiter this link's wants (loop thread)

  ble::ICentral& ble_;
  RadioArbiter& arb_;
  const RadioSlot slot_;

  std::string address_;         // guarded by mutex_
  AddrType addr_type_ = AddrType::kUnknown;  // guarded by mutex_
  std::string name_ = "Scale";  // guarded by mutex_
  // Consecutive failed known-type connects. After kKnownTypeFailLimit the
  // next attempt goes back to trying BOTH types and relearns from whichever
  // works — a stored type can never strand the link (scale firmware updates
  // have been known to change it). Link thread only.
  int known_type_fails_ = 0;
  std::function<void(AddrType)> addr_type_learned_;  // set once before run()

  // Model driver for the saved scale; refreshed per connection attempt.
  // Guarded by mutex_; shared_ptr so the notify callback + link loop keep a
  // replaced driver alive until they let go.
  std::shared_ptr<IScaleDriver> driver_;

  mutable std::mutex mutex_;  // guards the cached fields below
  bool connected_ = false;
  bool weight_streaming_ = false;
  float weight_g_ = 0.0f;
  uint32_t timer_ms_ = 0;
  int battery_pct_ = 0;
  bool battery_valid_ = false;
  uint32_t seq_ = 0;  // increments per weight update (snapshot.seq)
  // On-scale setting values as last reported (-1 = unknown). Reset on every
  // connect/disconnect: the scale is the owner, so a stale cache must never
  // outlive the link that read it. Written optimistically by
  // set_device_setting so the UI label moves at tap time; the next readback
  // (Bookoo: every weight frame, Acaia: every status frame) reconciles.
  int setting_value_[kMaxScaleSettings] = {-1, -1, -1, -1};
  // While non-zero, readback disagreeing with setting_value_ is a stale echo
  // of the pre-write value and is ignored (see on_device_setting).
  uint32_t setting_grace_until_ms_[kMaxScaleSettings] = {0, 0, 0, 0};
  // Resend cycle for unconfirmed setting writes (Acaia drops acked writes):
  // the value in flight, sends remaining, and when the next resend is due.
  int retry_value_[kMaxScaleSettings] = {-1, -1, -1, -1};
  uint8_t retry_left_[kMaxScaleSettings] = {0, 0, 0, 0};
  uint32_t retry_at_ms_[kMaxScaleSettings] = {0, 0, 0, 0};
  uint32_t pending_since_ms_[kMaxScaleSettings] = {0, 0, 0, 0};  // last tap
  uint32_t next_setting_write_ms_ = 0;  // write spacing (kSettingWriteGapMs)
  std::vector<ScanResult> scan_results_;

  std::atomic<bool> connect_enabled_{true};  // scales auto-connect when saved
  std::atomic<bool> partner_mode_{false};
  std::atomic<bool> partner_paired_{false};
  std::atomic<bool> partner_connected_{false};
  std::function<void(int, int)> partner_setting_writer_;
  std::atomic<bool> reconnect_requested_{false};
  std::atomic<bool> scanning_{false};
  std::atomic<bool> pending_tare_{false};
  // Posted on-scale setting writes (option index, -1 = none) — same
  // post-and-flush pattern as pending_tare_, one slot per descriptor.
  std::atomic<int> pending_setting_[kMaxScaleSettings] = {-1, -1, -1, -1};
};

}  // namespace core
