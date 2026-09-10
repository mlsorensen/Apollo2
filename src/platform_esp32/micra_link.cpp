#include "platform_esp32/micra_link.h"

#include <Arduino.h>

namespace platform {

namespace {
void task_entry(void* arg) { static_cast<core::MicraLink*>(arg)->run(); }
}  // namespace

void MicraLink::begin(std::string address) {
  set_address(std::move(address));
  // 4096: measured peak 2,364 -- live link, polling, AND a Settings scan
  // (do_scan runs on this task and is the deepest path). ~1.7K margin.
  xTaskCreatePinnedToCore(&task_entry, "micra_link", 4096, this,
                          /*priority=*/1, nullptr, /*core=*/1);
}

}  // namespace platform
