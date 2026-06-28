#pragma once

#include "core/machine.h"

// Host-only stand-in for a real machine. On the device this role is played by
// the BLE transport; here it just returns canned state so we can design and
// render the UI on a laptop. Keeping it behind core::IMachine means the UI
// code that renders from it is byte-for-byte the same code that will later
// render from the live BLE feed.

namespace host {

class FakeMachine : public core::IMachine {
 public:
  core::MachineSnapshot snapshot() const override;
  void set_power(bool on) override;

 private:
  core::Power power_ = core::Power::On;
};

}  // namespace host
