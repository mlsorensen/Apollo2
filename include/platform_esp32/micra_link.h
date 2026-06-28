#pragma once

#include <atomic>
#include <mutex>
#include <string>

#include "core/machine.h"

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
  explicit MicraLink(std::string auth_token);

  // Start the background connection task (non-blocking). Connects to `address`,
  // authenticates, polls state every few seconds, and reconnects on drop.
  void begin(std::string address);

  // core::IMachine — thread-safe cached read.
  core::MachineSnapshot snapshot() const override;

  // core::IMachine — post a power command for the task to apply (non-blocking).
  void set_power(bool on) override;

 private:
  static void task_entry(void* arg);
  void task_loop();
  bool do_connect();        // task thread: connect + discover + auth
  bool do_refresh();        // task thread: read + parse into the cache
  void do_set_power(bool on);  // task thread: write the command
  void set_link(core::Link link);

  std::string token_;
  std::string address_;

  mutable std::mutex mutex_;  // guards the cached fields below
  core::Link link_ = core::Link::Disconnected;
  core::Power power_ = core::Power::Off;
  float brew_temp_c_ = 0.0f;
  float brew_target_c_ = 0.0f;
  float boiler_temp_c_ = 0.0f;
  float boiler_target_c_ = 0.0f;
  bool brewing_ = false;

  std::atomic<int> pending_power_{-1};  // -1 none, 0 standby, 1 on
};

}  // namespace platform
