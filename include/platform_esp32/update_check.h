#pragma once

// Release check against the web-flasher site (contract in core/update_check.h).
// Notify-only on the hosted-radio boards. The old memory trap (mbedTLS's ~46KB
// of TLS buffers landing in the scarce internal-DMA pool the hosted C6 radio's
// SDIO driver shares, and starving it — sdio_rx_get_buffer assert) is fixed by
// pointing mbedTLS at PSRAM at runtime (use_psram_for_tls in the .cpp), so a
// check runs LIVE from main's loop with BLE up — no boot-window or restart. The
// fetch runs on its own short-lived task; main's loop spawns it on request.
// Self-install (run_install) stays DORMANT here: a sustained multi-MB download
// still exhausts the esp-hosted SDIO RX pool below where TCP flow control can
// see it. Kept wired for the S3 boards / a future rate-limited attempt.

#include <atomic>
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

  mutable std::mutex mu_;  // guards latest_/notes_/install_error_
  std::string latest_;
  std::string notes_;
  std::string install_error_;
};

}  // namespace platform
