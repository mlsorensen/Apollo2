#include "platform_esp32/micra_link.h"

#include <Arduino.h>

namespace platform {

namespace {
void task_entry(void* arg) { static_cast<core::MicraLink*>(arg)->run(); }
}  // namespace

void MicraLink::begin(std::string address) {
  set_address(std::move(address));
  xTaskCreatePinnedToCore(&task_entry, "micra_link", 8192, this,
                          /*priority=*/1, nullptr, /*core=*/1);
}

}  // namespace platform
