#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/provisioner.h"     // core::ScanResult
#include "core/radio_arbiter.h"   // core::FinderTargets / Sighting

// Core BLE central port: the small GATT-client vocabulary the device protocols
// (Micra, scales) are written against. Like every core header this stays free
// of LVGL, Arduino, NimBLE, and SDL — a platform provides the implementation
// (NimBLE on ESP32, BlueZ on Linux, btstack on a Pico, a scripted fake on the
// host), and the protocol code in src/core/ never changes across them.
//
// Scope is deliberately minimal — central role, one connection per instance,
// characteristics addressed by UUID string ("2a29", "ff11", or the full
// 128-bit "xxxxxxxx-xxxx-..." form). No advertising, bonding, or L2CAP.

namespace core::ble {

// Notification payload callback. Invoked from the platform's BLE thread; the
// receiver is responsible for its own locking.
using NotifyCallback = std::function<void(const uint8_t* data, size_t len)>;

class ICentral {
 public:
  virtual ~ICentral() = default;

  // Scan for advertising peripherals for `ms` (blocking; active scan so names
  // resolve). Returns every device seen; callers filter by name.
  virtual std::vector<core::ScanResult> scan(uint32_t ms) = 0;

  // Finder chunk (see core::RadioArbiter): a PASSIVE scan that matches only
  // the given addresses, stores nothing, and stops on the FIRST match.
  // Blocks for `ms`, or less when a target is seen or cancel_scan() lands.
  // Returns the sighting (found=false for an empty chunk). The default is
  // for hosts without a radio.
  virtual core::Sighting scan_for(const core::FinderTargets& /*targets*/, uint32_t /*ms*/) {
    return core::Sighting{};
  }
  // Stop a running scan (scan() or scan_for()) from another thread; the
  // blocked call returns with what it has. No-op when not scanning.
  virtual void cancel_scan() {}

  // Connect to `mac` and discover its services (blocking). A KNOWN `type`
  // is a single try at that type. kUnknown tries both, `prefer_random`
  // first (scales usually advertise a random address) — the wrong-type try
  // can never succeed, so callers learn the type (connected_addr_type) and
  // persist it to skip that dead time next connect. `timeout_ms` caps EACH
  // try (0 = the stack's default).
  virtual bool connect(const std::string& mac, core::AddrType type,
                       bool prefer_random, uint32_t timeout_ms) = 0;
  // Address type of the try that produced the current connection (kUnknown
  // when disconnected or the transport can't tell).
  virtual core::AddrType connected_addr_type() const { return core::AddrType::kUnknown; }
  virtual void disconnect() = 0;
  virtual bool connected() = 0;

  // Abort an IN-FLIGHT connect() attempt (makes the blocked call return false
  // promptly). Safe to call from another thread. No-op when nothing is
  // connecting. CALLERS: only core::RadioArbiter (through its hold hook) —
  // on NimBLE the cancel is GLOBAL (it aborts whichever link is initiating),
  // so a link must never cancel on its own behalf; the arbiter cancels the
  // holder and nobody else.
  virtual void cancel_connect() {}

  // LEVEL version of the above: while held, connect() fails fast — the current
  // attempt is aborted AND no new attempt may start (including the second
  // address-type try inside one connect() call). A one-shot cancel is not
  // enough for scan preemption: the cancelled connect() would fall through to
  // its other-address-type attempt, or the owning task could start a fresh
  // connect in the gap before the scan grabs the radio. Cross-thread safe.
  virtual void hold_connects(bool on) { if (on) cancel_connect(); }

  // Characteristic operations on the current connection. All return false when
  // disconnected or when the UUID wasn't discovered.
  virtual bool has_characteristic(const char* uuid) = 0;
  virtual bool read(const char* uuid, std::string& out) = 0;
  virtual bool write(const char* uuid, const void* data, size_t len,
                     bool with_response) = 0;
  virtual bool subscribe(const char* uuid, NotifyCallback cb) = 0;
};

}  // namespace core::ble
