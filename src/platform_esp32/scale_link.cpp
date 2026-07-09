#include "platform_esp32/scale_link.h"

#include <Arduino.h>

namespace platform {

namespace {
void task_entry(void* arg) { static_cast<core::ScaleLink*>(arg)->run(); }
}  // namespace

void ScaleLink::begin(std::string address) {
  set_address(std::move(address));
  xTaskCreatePinnedToCore(&task_entry, "scale_link", 8192, this,
                          /*priority=*/1, nullptr, /*core=*/1);
}

}  // namespace platform
