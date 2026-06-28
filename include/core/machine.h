#pragma once

// Core domain layer: the machine model and the interface the UI talks to.
//
// IMPORTANT: this header must stay free of LVGL, Arduino, BLE, and SDL. It is
// the stable center of the project. The on-device BLE transport, the host
// "fake" machine, and any future device (a second Micra, a GPIO relay, etc.)
// all become implementations of core::IMachine. The UI never knows or cares
// which one it's talking to.

namespace core {

// La Marzocco machines power-cycle between fully on and a low-power standby
// rather than a hard off, so we model three states.
enum class Power { Off, Standby, On };

// A flat, copyable snapshot of everything the UI needs to draw one frame.
// Commands (set power, start brew, change target) will be added to IMachine as
// methods later; reads stay in this struct so the UI can render from a value
// without holding a live reference while drawing.
//
// The Linea Micra has two thermal zones: the brew (coffee) boiler and the
// steam boiler. We surface both. Values are placeholders until the BLE link
// fills them in for real.
struct MachineSnapshot {
  const char* name;            // e.g. "Linea Micra"
  Power       power;
  float       brew_temp_c;     // brew/coffee boiler — current
  float       brew_target_c;   // brew/coffee boiler — set point
  float       boiler_temp_c;   // steam boiler — current
  float       boiler_target_c; // steam boiler — set point
  bool        brewing;         // a shot is currently being pulled
  const char* status;          // short human-readable status, e.g. "Ready"
};

// The port the UI depends on. Anything that can describe and control a machine
// implements this — the BLE link, the host fake, future devices. The UI holds
// only a reference to this interface, never a concrete transport.
class IMachine {
 public:
  virtual ~IMachine() = default;

  // Latest known state. Cheap and synchronous (a cached read) — implementations
  // must not block on I/O here.
  virtual MachineSnapshot snapshot() const = 0;

  // Command: switch the machine on (brew mode) or to standby. May block briefly
  // on a transport write; implementations should update their cached snapshot so
  // a subsequent snapshot() reflects the change.
  virtual void set_power(bool on) = 0;
};

}  // namespace core
