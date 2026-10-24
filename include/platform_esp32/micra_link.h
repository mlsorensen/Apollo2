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
  explicit MicraLink(core::RadioArbiter& arb) : core::MicraLink(central_, arb) {}

  // Start the background connection task (non-blocking). With both an address
  // and a token set, connects/authenticates/polls and reconnects on drop. Empty
  // address -> Unconfigured; address but no token -> NeedsToken; both idle.
  void begin(std::string address, core::AddrType type);

  // The transport, for the arbiter's hold / scan-cancel hooks (device main).
  NimbleCentral& central() { return central_; }

 private:
  NimbleCentral central_;
};

}  // namespace platform
