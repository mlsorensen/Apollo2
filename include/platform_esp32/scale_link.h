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
  ScaleLink(core::RadioArbiter& arb, core::RadioSlot slot)
      : core::ScaleLink(central_, arb, slot), partner_(*this) {}

  // Register the Umbra partner GATT service. Call ONCE, right after
  // NimBLEDevice::init() and BEFORE any link starts: NimBLE refuses GATT
  // changes while any connection, connect attempt, scan or advertising is
  // active, and NimBLEServer::start() turns that refusal into a
  // ble_svc_gap_init assert (bench, 2026-10-17: boot loop on the 4.3 with a
  // Micra connecting). Everything after this only starts/stops advertising.
  bool register_partner() { return partner_.begin(); }
  bool partner_registered() const { return partner_.ready(); }
  void hold_partner(bool held) { partner_.hold(held); }

  // Seed the link and start its background task (non-blocking). The espresso
  // link's task always runs (it also serves the Settings scan); the bench
  // link's starts only once a bench scale is saved — at boot with a saved
  // address, or on the first save via set_address — so units without one
  // never pay its 4 KB stack.
  void begin(std::string address, core::AddrType type);
  void set_address(std::string address, core::AddrType type = core::AddrType::kUnknown);
  void set_name(std::string name);
  void set_partner_mode(bool enabled);
  void set_connect_enabled(bool enabled);
  // Screensaver / setup portal: let a partner-mode Umbra doze (it is sent to
  // sleep and advertising stops). Radio arbitration never comes through here
  // — yielding the radio to another link must not put the Umbra to sleep,
  // which the old shared pause level did on every Micra Settings scan.
  void pause_partner(bool paused);
  void poll_partner() { partner_.poll(); }

  // The transport, for the arbiter's hold / scan-cancel hooks (device main).
  NimbleCentral& central() { return central_; }

 private:
  void start_task();

  NimbleCentral central_;
  UmbraPartner partner_;
  bool task_started_ = false;
};

}  // namespace platform
