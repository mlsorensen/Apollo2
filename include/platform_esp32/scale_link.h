#pragma once

#include <string>

#include "core/scale_link.h"
#include "platform_esp32/nimble_central.h"
#include "platform_esp32/umbra_partner.h"

// Device-side ScaleLink: the portable core::ScaleLink protocol (Bookoo Themis
// driver) running over the NimBLE transport, on a dedicated FreeRTOS task
// started by begin(). One NimBLE host holds this connection concurrently with
// MicraLink's (NimBLE default max-connections = 3).

namespace platform {

class ScaleLink : public core::ScaleLink {
 public:
  ScaleLink() : core::ScaleLink(central_), partner_(*this) {}

  // Start the background task (non-blocking). Empty address -> idle until set.
  void begin(std::string address);
  void set_name(std::string name);
  void set_partner_mode(bool enabled);
  void set_connect_enabled(bool enabled);
  void pause_connects(bool paused);
  void poll_partner() { partner_.poll(); }

 private:
  NimbleCentral central_;
  UmbraPartner partner_;
};

}  // namespace platform
