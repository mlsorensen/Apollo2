#pragma once

#include <string>

#include "core/micra_link.h"
#include "platform_esp32/nimble_central.h"

// Device-side MicraLink: the portable core::MicraLink protocol running over the
// NimBLE transport, on a dedicated FreeRTOS task started by begin(). All the
// protocol logic (connect, auth, polling, pairing-token read, scan filtering)
// lives in src/core/micra_link.cpp; this class only supplies the transport and
// the thread.

namespace platform {

class MicraLink : public core::MicraLink {
 public:
  MicraLink() : core::MicraLink(central_) {}

  // Start the background connection task (non-blocking). With both an address
  // and a token set, connects/authenticates/polls and reconnects on drop. Empty
  // address -> Unconfigured; address but no token -> NeedsToken; both idle.
  void begin(std::string address);

 private:
  NimbleCentral central_;
};

}  // namespace platform
