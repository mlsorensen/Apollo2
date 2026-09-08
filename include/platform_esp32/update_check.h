#pragma once

// Release check against the web-flasher site (contract in core/update_check.h).
// Notify-only on the hosted-radio boards. The old memory trap (mbedTLS's ~46KB
// of TLS buffers landing in the scarce internal-DMA pool the hosted C6 radio's
// SDIO driver shares, and starving it — sdio_rx_get_buffer assert) is fixed by
// pointing mbedTLS at PSRAM at runtime (use_psram_for_tls in the .cpp), so a
// check runs LIVE from main's loop with BLE up — no boot-window or restart. The
// fetch runs on its own short-lived task; main's loop spawns it on request.
//
// EXPERIMENTAL self-install (this branch): run_install pulls the app image in
// ranged HTTP pieces (kChunk, with an idle gap and an esp_ota_write between
// each) and, crucially, main FULLY PARKS BOTH BLE LINKS for the duration
// (set_ble_park below) so the hosted radio isn't fighting the SDIO download for
// the internal-DMA pool that overflowed the RX ring before. Success reboots
// into the new slot; any failure/cancel leaves the running image untouched
// (rollback-armed) and restores BLE.

#include <atomic>
#include <functional>
#include <mutex>

#include "core/update_check.h"

namespace platform {

class Config;
class Network;

class UpdateCheck : public core::IUpdateSource {
 public:
  UpdateCheck(Config& config, Network& network);

  // --- called by main's loop ---
  bool network_ready() const;   // station up + NTP synced (else all inert)
  bool take_check_request();    // manual check requested?
  void begin_check();           // spawn the live fetch task
  bool task_active() const { return in_flight_.load(); }

  // Called synchronously as an install begins (park=true, before the TLS
  // handshake) and after it fails/cancels (park=false). main wires this to
  // fully drop both BLE links so the hosted radio isn't contending with the
  // SDIO download for the internal-DMA pool. Not called on success (we reboot).
  void set_ble_park(std::function<void(bool)> h) { ble_park_ = std::move(h); }

  // --- core::IUpdateSource ---
  core::UpdateInfo info() const override;
  void skip_current() override;
  bool check_at_startup() const override;
  void set_check_at_startup(bool on) override;
  void request_check() override { check_req_.store(true); }
  bool checking() const override { return check_req_.load() || in_flight_.load(); }
  int check_seq() const override { return seq_.load(); }
  void start_install() override;  // spawn the install task (dormant on P4)
  void cancel_install() override { cancel_.store(true); }
  core::InstallStatus install_status() const override;

 private:
  static void check_task_entry(void* arg);
  static void install_task_entry(void* arg);
  void run_check();
  void run_install();

  Config& config_;
  Network& network_;
  std::atomic<bool> in_flight_{false};
  std::atomic<bool> check_req_{false};
  std::atomic<int> seq_{0};
  std::atomic<bool> cancel_{false};

  std::atomic<int> install_state_{0};  // core::InstallState
  std::atomic<int> install_pct_{0};

  std::function<void(bool)> ble_park_;  // set_ble_park(): drop/restore BLE links

  mutable std::mutex mu_;  // guards latest_/notes_/install_error_
  std::string latest_;
  std::string notes_;
  std::string install_error_;
};

}  // namespace platform
