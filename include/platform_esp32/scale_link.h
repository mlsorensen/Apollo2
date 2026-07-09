#pragma once

#include <string>

#include "core/scale_link.h"
#include "platform_esp32/nimble_central.h"

// Device-side ScaleLink: the portable core::ScaleLink protocol (Bookoo Themis
// driver) running over the NimBLE transport, on a dedicated FreeRTOS task
// started by begin(). One NimBLE host holds this connection concurrently with
// MicraLink's (NimBLE default max-connections = 3).

namespace platform {

class ScaleLink : public core::ScaleLink {
 public:
  ScaleLink() : core::ScaleLink(central_) {}

  // Start the background task (non-blocking). Empty address -> idle until set.
  void begin(std::string address);

 private:
  NimbleCentral central_;
};

}  // namespace platform
