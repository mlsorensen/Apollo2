#pragma once

// Release CHECK against the releases site (contract in core/update_check.h).
// The memory trap (mbedTLS's ~46KB of TLS record buffers landing in the scarce
// internal-DMA pool the hosted C6 radio's SDIO driver shares, and starving it —
// sdio_rx_get_buffer assert) is avoided by pointing mbedTLS at PSRAM at runtime
// (use_psram_for_tls in the .cpp), so a small check fetch runs LIVE from main's
// loop with BLE up. The fetch runs on its own short-lived task; main's loop
// spawns it on request. INSTALLING is a separate concern — see
// platform_esp32/install_mode.h (early-boot download + flash).

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
  int check_cadence() const override;
  void set_check_cadence(int mode) override;
  void request_check() override { check_req_.store(true); }
  bool checking() const override { return check_req_.load() || in_flight_.load(); }
  int check_seq() const override { return seq_.load(); }
  bool last_check_ok() const override { return last_ok_.load(); }

 private:
  static void check_task_entry(void* arg);
  void run_check();

  Config& config_;
  Network& network_;
  std::atomic<bool> in_flight_{false};
  std::atomic<bool> check_req_{false};
  std::atomic<int> seq_{0};
  std::atomic<bool> last_ok_{false};  // last check reached the server + parsed

  mutable std::mutex mu_;  // guards latest_/notes_
  std::string latest_;
  std::string notes_;
};

}  // namespace platform
