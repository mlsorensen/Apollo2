#include "platform_host/fake_machine.h"

namespace host {

core::MachineSnapshot FakeMachine::snapshot() const {
  return core::MachineSnapshot{
      .name = "Linea Micra",
      .manufacturer = "La Marzocco",
      .model = "Linea Micra",
      .serial = "MR002018",
      .firmware = "1.40",
      .software = "v5.0.9",
      .link = core::Link::Connected,  // the fake is always "connected"
      .power = power_,
      .brew_temp_c = 93.0f,
      .brew_target_c = brew_target_,
      .boiler_temp_c = 123.0f,
      .boiler_target_c = steam_target_,
      .steam_enabled = steam_enabled_,
      .brewing = false,
  };
}

void FakeMachine::set_power(bool on) {
  power_ = on ? core::Power::On : core::Power::Standby;
}

void FakeMachine::set_brew_target(float celsius) { brew_target_ = celsius; }
void FakeMachine::set_steam_target(float celsius) { steam_target_ = celsius; }
void FakeMachine::set_steam_enabled(bool enabled) { steam_enabled_ = enabled; }

}  // namespace host
