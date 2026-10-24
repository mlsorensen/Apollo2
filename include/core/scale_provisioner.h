#pragma once

#include <string>
#include <vector>

#include "core/provisioner.h"  // reuse core::ScanResult

// Discovery + setup port for Bluetooth scales — the scale-side analogue of
// IProvisioner. The UI scans for supported scales, saves the chosen one (its MAC
// to NVS), and the device reconnects to it on boot. Unlike the Micra, a scale
// needs no auth token, so this port is a trimmed IProvisioner.
//
// TWO SLOTS (v1.2): the ESPRESSO scale owns every shot role (auto-stop, the
// tare at shot start, shot detect, the auto-flush cup-off, the graph); the
// BENCH scale is a weight readout with its own Tare — a counter scale whose
// screen a plate hides. The slot IS the role: "Swap roles" exchanges the two
// saved scales' keys and, at runtime, which link holds which role
// (core::ScaleRoles) — the links keep their connections. Every save goes
// through the same flow — Scan, tap a result, choose Espresso or Bench —
// into that slot, replacing whatever was there; nothing is ever auto-slotted.

namespace core {

enum class ScaleSlot : uint8_t { kEspresso = 0, kBench = 1 };

inline const char* scale_slot_name(ScaleSlot s) {
  return s == ScaleSlot::kBench ? "bench" : "espresso";
}

class IScaleProvisioner {
 public:
  virtual ~IScaleProvisioner() = default;

  virtual void start_scan() = 0;                       // non-blocking
  virtual bool scanning() const = 0;
  virtual std::vector<ScanResult> scan_results() const = 0;

  // Persist the chosen scale (mac + name) into `slot` and start connecting to
  // it. Returns false when refused: partner mode was asked for while the
  // OTHER slot already has it (one partner-mode Umbra at a time — the device
  // has one partner GATT service and one inbound session).
  virtual bool save_scale(const ScanResult& scale, ScaleSlot slot,
                          bool partner_mode = false) = 0;

  // The slot's saved scale display name, or "" if none is saved. The UI uses
  // the espresso one to decide whether to show the Scale pages + scale-aware
  // Home at all, and either to decide the bench readout.
  virtual std::string saved_name(ScaleSlot slot = ScaleSlot::kEspresso) const = 0;

  // Exchange the two saved scales (address, name, type, partner flag). With
  // one slot empty this moves the scale into the other slot.
  virtual void swap_roles() = 0;

  // Umbra partner mode (fw 1.0.8). The partner GATT service can only be
  // registered at boot, before any BLE link is active, and only when partner
  // mode is saved — so choosing an Umbra may need ONE restart to get ready.
  // At most one slot is in partner mode; these address THAT slot.
  virtual bool partner_pairing_complete() const { return false; }
  virtual bool partner_ready() const { return false; }  // registered this boot
  // The slot in partner mode, or -1 for none.
  virtual int partner_slot() const { return -1; }
  // "Show the pairing prompt after the restart": set before rebooting for an
  // Umbra, read + cleared by the UI on the next boot.
  virtual bool partner_pair_pending() const { return false; }
  virtual void set_partner_pair_pending(bool /*pending*/) {}
  // Switch the partner slot's SAVED scale out of (or back into) partner mode
  // (e.g. "Use unpaired").
  virtual void set_partner_mode(bool /*enabled*/) {}
  // Hold off partner advertising while the pairing prompt's grace period runs.
  virtual void hold_partner(bool /*held*/) {}

  // Clear the slot's saved scale -> nothing configured there.
  virtual void forget(ScaleSlot slot = ScaleSlot::kEspresso) = 0;

  // Manual connect control (mirrors IProvisioner): a battery remote can drop a
  // scale link to save power. Meaningful once a scale is saved in the slot.
  virtual bool connect_enabled(ScaleSlot slot = ScaleSlot::kEspresso) const = 0;
  virtual void set_connect_enabled(ScaleSlot slot, bool enabled) = 0;
};

}  // namespace core
