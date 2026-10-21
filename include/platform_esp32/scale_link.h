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

  // Register the Umbra partner GATT service. Call ONCE, right after
  // NimBLEDevice::init() and BEFORE any link starts: NimBLE refuses GATT
  // changes while any connection, connect attempt, scan or advertising is
  // active, and NimBLEServer::start() turns that refusal into a
  // ble_svc_gap_init assert (bench, 2026-10-17: boot loop on the 4.3 with a
  // Micra connecting). Everything after this only starts/stops advertising.
  bool register_partner() { return partner_.begin(); }
  bool partner_registered() const { return partner_.ready(); }
  void hold_partner(bool held) { partner_.hold(held); }

  // Start the background task (non-blocking). Empty address -> idle until set.
  void begin(std::string address, core::AddrType type);
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
