#pragma once

#include <atomic>
#include <string>
#include <vector>

#include "core/ble.h"

// NimBLE implementation of the core::ble::ICentral port — the ESP32's GATT
// transport. One instance per link (each owns one NimBLEClient); the NimBLE
// host itself is a process-wide singleton initialised on first use (or earlier
// by device main), and holds all links' connections concurrently (default
// max-connections = 3).

class NimBLEClient;  // fwd — keep NimBLE headers out of everything upstream

namespace platform {

// Log connection parameter updates and peer requests through NimBLE's custom
// GAP handler. Call once after NimBLEDevice::init().
void install_gap_event_log();

class NimbleCentral : public core::ble::ICentral {
 public:
  NimbleCentral() = default;

  std::vector<core::ScanResult> scan(uint32_t ms) override;
  core::Sighting scan_for(const core::FinderTargets& targets, uint32_t ms) override;
  void cancel_scan() override;
  bool connect(const std::string& mac, core::AddrType type, bool prefer_random,
               uint32_t timeout_ms) override;
  core::AddrType connected_addr_type() const override { return conn_type_; }
  void disconnect() override;
  bool connected() override;
  void cancel_connect() override;
  void hold_connects(bool on) override;
  // Connection parameters this central asks for when it dials (units: 1.25 ms
  // intervals, 10 ms timeout). Unset = NimBLE's defaults (50 ms / 2.56 s).
  // The Micra's link uses a tighter interval so its events compete with a
  // partner Umbra's 11.25 ms stream (see micra-gatt-stall-drops).
  void set_preferred_conn_params(uint16_t min_itvl, uint16_t max_itvl, uint16_t latency,
                                 uint16_t timeout) {
    pref_min_ = min_itvl; pref_max_ = max_itvl; pref_latency_ = latency; pref_timeout_ = timeout;
  }

  bool has_characteristic(const char* uuid) override;
  bool read(const char* uuid, std::string& out) override;
  bool write(const char* uuid, const void* data, size_t len,
             bool with_response) override;
  bool subscribe(const char* uuid, core::ble::NotifyCallback cb) override;

 private:
  NimBLEClient* client_ = nullptr;  // created lazily; reused across connects
  // While set, connect() fails fast (checked before EACH address-type attempt)
  // and the in-flight attempt was cancelled — see ICentral::hold_connects.
  std::atomic<bool> hold_{false};
  // One-shot: cancel_connect() aborts the whole in-flight connect() (both
  // address-type attempts), not just the current GAP attempt. Cleared at the
  // start of each connect() call.
  std::atomic<bool> cancelled_{false};
  // Set around client_->connect(): NimBLE's connect-cancel is GLOBAL (it
  // aborts whichever client is initiating), so cancel_connect() is a no-op
  // unless THIS instance is the one connecting.
  std::atomic<bool> connecting_{false};
  core::AddrType conn_type_ = core::AddrType::kUnknown;  // type of the live link
  uint16_t pref_min_ = 0, pref_max_ = 0, pref_latency_ = 0, pref_timeout_ = 0;  // 0 = default
};

}  // namespace platform
