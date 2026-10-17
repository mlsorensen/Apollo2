#include "platform_esp32/scale_link.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include "core/system.h"

namespace platform {

namespace {
void task_entry(void* arg) { static_cast<core::ScaleLink*>(arg)->run(); }
}  // namespace

void ScaleLink::begin(std::string address) {
  partner_.set_enabled(partner_mode() && connect_enabled());
  set_address(std::move(address));
  // 4096: measured peak 2,356 (Acaia) / 2,300 (Bookoo), both connected +
  // subscribed, and including a Settings scan. ~1.7K margin.
  xTaskCreatePinnedToCore(&task_entry, "scale_link", 4096, this,
                          /*priority=*/1, nullptr, /*core=*/1);
}

void ScaleLink::set_name(std::string name) {
  core::ScaleLink::set_name(std::move(name));
}

void ScaleLink::set_partner_mode(bool enabled) {
  core::ScaleLink::set_partner_mode(enabled);
  // Never registers here — see register_partner(). A no-op if registration
  // failed at boot (poll() stays idle).
  partner_.set_enabled(enabled && connect_enabled());
}

void ScaleLink::set_connect_enabled(bool enabled) {
  core::ScaleLink::set_connect_enabled(enabled);
  if (partner_mode()) partner_.set_enabled(enabled);
}

void ScaleLink::pause_connects(bool paused) {
  core::ScaleLink::pause_connects(paused);
  if (partner_mode()) partner_.pause(paused);
}

}  // namespace platform
