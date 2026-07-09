#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "core/ble.h"
#include "core/machine.h"
#include "core/provisioner.h"

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
  explicit MicraLink(ble::ICentral& ble) : ble_(ble) {}

  // The connection loop: connect/authenticate/poll and reconnect on drop.
  // Blocks forever — call from a dedicated thread (the platform's begin()).
  // With no address -> Unconfigured; address but no token -> NeedsToken.
  void run();

  // Runtime setters (thread-safe; trigger a reconnect). Address/token empty
  // leave the link idle in the corresponding state.
  void set_address(std::string address);
  void set_token(std::string token);
  void set_name(std::string name);  // display name for the snapshot

  // Try to read the auth token from the machine's pairing-mode characteristic
  // (works only while the machine is in pairing mode). On success the token is
  // adopted (and persisted via the persister) and the link connects; on failure
  // the link settles in NeedsToken. Non-blocking; runs on the loop thread.
  void request_pairing_read();

  // Persist callback for a token obtained via pairing read (the device wires
  // this to NVS). Set once before the loop starts.
  void set_token_persister(std::function<void(std::string)> persister);

  // Manual connect gate. Disabling drops the link and stops auto-reconnect.
  bool connect_enabled() const { return connect_enabled_.load(); }
  void set_connect_enabled(bool enabled);

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
  bool do_connect(const std::string& address, const std::string& token);
  bool do_refresh();           // loop thread: read + parse into the cache
  void do_set_power(bool on);  // loop thread: write the command
  void do_set_boiler_target(const char* identifier, const char* value);
  void do_set_steam_enabled(bool enabled);
  void do_scan();              // loop thread: scan + publish results
  std::string do_read_pairing_token(const std::string& address);
  void set_link(Link link);
  bool read_setting(const char* name, std::string& out);

  ble::ICentral& ble_;

  std::string token_;           // guarded by mutex_
  std::string address_;         // guarded by mutex_
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

  std::atomic<int> pending_power_{-1};         // -1 none, 0 standby, 1 on
  std::atomic<int> pending_brew_tenths_{-1};   // target*10, -1 none
  std::atomic<int> pending_steam_whole_{-1};   // target C, -1 none
  std::atomic<int> pending_steam_enable_{-1};  // 0 off, 1 on, -1 none
  std::atomic<bool> reconnect_requested_{false};
  // Boot DISCONNECTED: don't auto-grab the Micra's single BLE link on startup (the
  // user picks a device + taps Connect). Setting up a device (save/pair) enables it.
  std::atomic<bool> connect_enabled_{false};  // user gate; false => drop + don't reconnect
  std::atomic<bool> try_pairing_{false};
  std::atomic<bool> token_bad_{false};  // authed but reads rejected -> needs re-entry
  std::atomic<bool> scan_requested_{false};
  std::function<void(std::string)> token_persister_;  // set once before the loop
  std::atomic<bool> scanning_{false};
  std::vector<ScanResult> scan_results_;  // guarded by mutex_
};

}  // namespace core
