#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "core/machine.h"
#include "core/provisioner.h"

// BLE client for the La Marzocco Micra, implementing core::IMachine.
//
// All BLE work (connect, auth, polling, auto-reconnect) runs in a dedicated
// FreeRTOS task started by begin(), so it never blocks the UI/main loop. The
// task updates a mutex-guarded cached snapshot; snapshot() reads it safely from
// the main thread. Commands (set_power) are posted to the task rather than
// executed inline, keeping all NimBLE access on the one task thread.

namespace platform {

class MicraLink : public core::IMachine {
 public:
  MicraLink() = default;

  // Start the background connection task (non-blocking). With both an address
  // and a token set, connects/authenticates/polls and reconnects on drop. Empty
  // address -> Unconfigured; address but no token -> NeedsToken; both idle.
  void begin(std::string address);

  // Runtime setters (thread-safe; trigger a reconnect). Address/token empty
  // leave the link idle in the corresponding state.
  void set_address(std::string address);
  void set_token(std::string token);
  void set_name(std::string name);  // display name for the snapshot

  // Try to read the auth token from the machine's pairing-mode characteristic
  // (works only while the machine is in pairing mode). On success the token is
  // adopted (and persisted via the persister) and the link connects; on failure
  // the link settles in NeedsToken. Non-blocking; runs on the task.
  void request_pairing_read();

  // Persist callback for a token obtained via pairing read (the device wires
  // this to NVS). Set once before begin().
  void set_token_persister(std::function<void(std::string)> persister);

  // core::IMachine — thread-safe cached read.
  core::MachineSnapshot snapshot() const override;

  // core::IMachine — post commands for the task to apply (non-blocking).
  void set_power(bool on) override;
  void set_brew_target(float celsius) override;
  void set_steam_target(float celsius) override;
  void set_steam_enabled(bool enabled) override;

  // Discovery, run on the BLE task. request_scan() is non-blocking; results are
  // published when the scan finishes. Used by the device Provisioner.
  void request_scan();
  bool scanning() const;
  std::vector<core::ScanResult> scan_results() const;

 private:
  static void task_entry(void* arg);
  void task_loop();
  bool do_connect(const std::string& address, const std::string& token);
  bool do_refresh();        // task thread: read + parse into the cache
  void do_set_power(bool on);  // task thread: write the command
  void do_set_boiler_target(const char* identifier, const char* value);
  void do_set_steam_enabled(bool enabled);
  void do_scan();           // task thread: scan + publish results
  std::string do_read_pairing_token(const std::string& address);
  void set_link(core::Link link);

  std::string token_;     // guarded by mutex_
  std::string address_;   // guarded by mutex_
  std::string name_ = "Micra";  // guarded by mutex_

  mutable std::mutex mutex_;  // guards the cached fields below
  // Device Information Service strings, read once per connect (guarded by mutex_).
  std::string dis_manufacturer_, dis_model_, dis_serial_, dis_firmware_, dis_software_;
  core::Link link_ = core::Link::Unconfigured;
  core::Power power_ = core::Power::Off;
  float brew_temp_c_ = 0.0f;
  float brew_target_c_ = 0.0f;
  float boiler_temp_c_ = 0.0f;
  float boiler_target_c_ = 0.0f;
  bool steam_enabled_ = true;
  bool brewing_ = false;

  std::atomic<int> pending_power_{-1};        // -1 none, 0 standby, 1 on
  std::atomic<int> pending_brew_tenths_{-1};  // target*10, -1 none
  std::atomic<int> pending_steam_whole_{-1};  // target C, -1 none
  std::atomic<int> pending_steam_enable_{-1}; // 0 off, 1 on, -1 none
  std::atomic<bool> reconnect_requested_{false};
  std::atomic<bool> try_pairing_{false};
  std::atomic<bool> token_bad_{false};  // authed but reads rejected -> needs re-entry
  std::atomic<bool> scan_requested_{false};
  std::function<void(std::string)> token_persister_;  // set once before begin()
  std::atomic<bool> scanning_{false};
  std::vector<core::ScanResult> scan_results_;  // guarded by mutex_
};

}  // namespace platform
