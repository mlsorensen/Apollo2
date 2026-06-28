#pragma once

#include <atomic>
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

  // core::IMachine — thread-safe cached read.
  core::MachineSnapshot snapshot() const override;

  // core::IMachine — post a power command for the task to apply (non-blocking).
  void set_power(bool on) override;

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
  void do_scan();           // task thread: scan + publish results
  void set_link(core::Link link);

  std::string token_;     // guarded by mutex_
  std::string address_;   // guarded by mutex_
  std::string name_ = "Micra";  // guarded by mutex_

  mutable std::mutex mutex_;  // guards the cached fields below
  core::Link link_ = core::Link::Unconfigured;
  core::Power power_ = core::Power::Off;
  float brew_temp_c_ = 0.0f;
  float brew_target_c_ = 0.0f;
  float boiler_temp_c_ = 0.0f;
  float boiler_target_c_ = 0.0f;
  bool brewing_ = false;

  std::atomic<int> pending_power_{-1};      // -1 none, 0 standby, 1 on
  std::atomic<bool> reconnect_requested_{false};
  std::atomic<bool> scan_requested_{false};
  std::atomic<bool> scanning_{false};
  std::vector<core::ScanResult> scan_results_;  // guarded by mutex_
};

}  // namespace platform
