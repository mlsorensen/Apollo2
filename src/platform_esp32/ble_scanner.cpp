#include "platform_esp32/ble_scanner.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <string>

namespace platform::ble {

void scan_and_report(int seconds) {
  NimBLEDevice::init("micra-remote");

  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);  // also request scan-response (gets the full name)
  scan->setInterval(45);
  scan->setWindow(15);

  Serial.printf("BLE: scanning for %d s...\n", seconds);
  NimBLEScanResults results = scan->getResults(seconds * 1000, false);

  const int count = results.getCount();
  Serial.printf("BLE: found %d device(s)\n", count);
  for (int i = 0; i < count; ++i) {
    const NimBLEAdvertisedDevice* d = results.getDevice(i);
    const std::string name = d->getName();
    const bool is_micra = name.rfind("MICRA", 0) == 0;  // starts-with
    Serial.printf("  %-18s rssi=%-4d name='%s'%s\n",
                  d->getAddress().toString().c_str(), d->getRSSI(),
                  name.c_str(), is_micra ? "   <-- MICRA" : "");
  }

  scan->clearResults();
}

}  // namespace platform::ble
