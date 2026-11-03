#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "core/ble.h"
#include "core/machine.h"
#include "core/provisioner.h"
#include "core/radio_arbiter.h"

// The La Marzocco Micra BLE protocol, implementing core::IMachine. Portable:
// written only against the core::ble::ICentral port + core/system.h shims, so
// the same code drives the machine from an ESP32 (NimBLE), a Pi (BlueZ), or
// any other platform that supplies an ICentral.
//
// All BLE work (connect, auth, polling, auto-reconnect) happens inside run(),
// which blocks forever — the platform calls it from a dedicated thread/task.
// run() updates a mutex-guarded cached snapshot; snapshot() reads it safely
// from the UI thread. Commands (set_power, ...) are posted to the loop rather
// than executed inline, keeping all transport access on the one thread.

namespace core {

class MicraLink : public IMachine {
 public:
  // Every listen on the radio (reconnects, the user's Settings scan) is
  // granted by `arb` (slot kMicra) and run on this link's own thread.
  MicraLink(ble::ICentral& ble, RadioArbiter& arb) : ble_(ble), arb_(arb) {}

  // The connection loop: connect/authenticate/poll and reconnect on drop.
  // Blocks forever — call from a dedicated thread (the platform's begin()).
  // With no address -> Unconfigured; address but no token -> NeedsToken.
  void run();

  // Runtime setters (thread-safe; trigger a reconnect). Address/token empty
  // leave the link idle in the corresponding state. `type` is the machine's
  // address type as learned on a previous connect (kUnknown = not yet: the
  // transport tries both, public first, and the link learns it).
  void set_address(std::string address, AddrType type = AddrType::kUnknown);
  void set_token(std::string token);
  void set_name(std::string name);  // display name for the snapshot
  // Called (from the link thread) when a connect LEARNS the address type;
  // the platform persists it so later connects are one try, not two — a
  // two-type try holds the radio for 16 s, which the scales' finder must
  // share.
  void set_addr_type_learned(std::function<void(AddrType)> cb) {
    addr_type_learned_ = std::move(cb);
  }
  // Watchdog for a request the machine never answers. The host's own GATT
  // timeout is 30 s, after which it drops the link anyway; a request still
  // pending after kRequestStallMs is cut short by dropping the link now and
  // re-dialling (every stall seen on the X-8, 2026-10-31, was a loss that
  // ran the full 30 s, never a late answer). The re-dial is masked: the
  // link reads Connected until it either succeeds (a ~10 s gap in fresh
  // state instead of ~35 s) or fails (then Disconnected, honestly). Call
  // from any other task every few ms with its clock; true = it fired.
  bool abort_stalled_request(uint32_t now_ms);
  static constexpr uint32_t kRequestStallMs = 5000;

  // Manual connect gate. Disabling drops the link and stops auto-reconnect.
  bool connect_enabled() const { return connect_enabled_.load(); }
  void set_connect_enabled(bool enabled);

  // State-poll cadence (machineMode + boilers). Thread-safe; takes effect on
  // the next loop pass. A refresh itself occupies the loop for ~0.5s (two
  // write->settle->read exchanges), so keep this comfortably above that.
  void set_poll_interval_ms(uint32_t ms) { poll_interval_ms_.store(ms); }
  uint32_t poll_interval_ms() const { return poll_interval_ms_.load(); }

  // core::IMachine — thread-safe cached read.
  MachineSnapshot snapshot() const override;

  // core::IMachine — post commands for the loop to apply (non-blocking).
  void set_power(bool on) override;
  void set_brew_target(float celsius) override;
  void set_steam_target(float celsius) override;
  void set_steam_enabled(bool enabled) override;

  // Discovery, run on the loop thread. request_scan() is non-blocking; results
  // are published when the scan finishes. Used by the device Provisioner.
  void request_scan();
  bool scanning() const;
  std::vector<ScanResult> scan_results() const;

 private:
  bool do_connect(const std::string& address, const std::string& token, AddrType type);
  bool do_refresh();           // loop thread: read + parse into the cache
  // Loop thread: write the command; false = the write failed (the caller
  // puts the command back in its slot).
  bool do_set_power(bool on);
  bool do_set_boiler_target(const char* identifier, const char* value);
  bool do_set_steam_enabled(bool enabled);
  void do_scan();              // loop thread: scan + publish results
  void set_link(Link link);
  bool read_setting(const char* name, std::string& out);

  ble::ICentral& ble_;
  RadioArbiter& arb_;

  std::string token_;           // guarded by mutex_
  std::string address_;         // guarded by mutex_
  AddrType addr_type_ = AddrType::kUnknown;  // guarded by mutex_
  std::function<void(AddrType)> addr_type_learned_;  // set once before run()
  uint32_t connect_fails_ = 0;  // consecutive; the log keeps the 1st, 2nd, then every 20th
  std::atomic<uint32_t> request_started_ms_{0};  // a GATT request in flight (0 = none)
  std::atomic<bool> recovering_{false};          // re-dialling after a stalled request
  std::atomic<uint32_t> recovering_since_ms_{0};
  static constexpr uint32_t kRecoverMaskMs = 15000;  // longer than that is a real outage
  bool refresh_bad_data_ = false;  // do_refresh: the machine answered, but not with JSON (link task)
  // Consecutive failed known-type connects. After kKnownTypeFailLimit the
  // next try goes back to BOTH types and relearns — a stored type can never
  // strand the link (the scale link has the same valve). Link thread only.
  int known_type_fails_ = 0;
  std::string name_ = "Micra";  // guarded by mutex_

  mutable std::mutex mutex_;  // guards the cached fields below
  // Device Information Service strings, read once per connect (guarded by mutex_).
  std::string dis_manufacturer_, dis_model_, dis_serial_, dis_firmware_, dis_software_;
  Link link_ = Link::Unconfigured;
  Power power_ = Power::Off;
  float brew_temp_c_ = 0.0f;
  float brew_target_c_ = 0.0f;
  float boiler_temp_c_ = 0.0f;
  float boiler_target_c_ = 0.0f;
  bool steam_enabled_ = true;
  bool brewing_ = false;

  std::atomic<uint32_t> poll_interval_ms_{1000};
  std::atomic<int> pending_power_{-1};         // -1 none, 0 standby, 1 on
  std::atomic<int> pending_brew_tenths_{-1};   // target*10, -1 none
  std::atomic<int> pending_steam_whole_{-1};   // target C, -1 none
  std::atomic<int> pending_steam_enable_{-1};  // 0 off, 1 on, -1 none
  std::atomic<bool> reconnect_requested_{false};
  // Boot DISCONNECTED: don't auto-grab the Micra's single BLE link on startup (the
  // user picks a device + taps Connect). Setting up a device (save/pair) enables it.
  std::atomic<bool> connect_enabled_{false};  // user gate; false => drop + don't reconnect
  std::atomic<bool> token_bad_{false};  // authed but reads rejected -> needs re-entry
  std::atomic<uint32_t> config_mode_seq_{0};  // bumped when seen in config mode (see snapshot)
  std::atomic<bool> scanning_{false};
  std::vector<ScanResult> scan_results_;  // guarded by mutex_
};

}  // namespace core
