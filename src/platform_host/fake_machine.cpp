#include "platform_host/fake_machine.h"

namespace host {

core::MachineSnapshot FakeMachine::snapshot() const {
  return core::MachineSnapshot{
      .name = "Linea Micra",
      .link = core::Link::Connected,  // the fake is always "connected"
      .power = power_,
      .brew_temp_c = 93.0f,
      .brew_target_c = 93.0f,
      .boiler_temp_c = 123.0f,
      .boiler_target_c = 125.0f,
      .brewing = false,
  };
}

void FakeMachine::set_power(bool on) {
  power_ = on ? core::Power::On : core::Power::Standby;
}

}  // namespace host
