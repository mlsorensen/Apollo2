#include "platform_host/fake_machine.h"

namespace host {

core::MachineSnapshot FakeMachine::snapshot() const {
  return core::MachineSnapshot{
      .name = "Linea Micra",
      .power = core::Power::On,
      .brew_temp_c = 93.0f,
      .brew_target_c = 93.0f,
      .boiler_temp_c = 123.0f,
      .boiler_target_c = 125.0f,
      .brewing = false,
      .status = "Ready",
  };
}

}  // namespace host
