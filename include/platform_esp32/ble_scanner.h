#pragma once

// Diagnostic BLE scanner. Scans for `seconds` and prints every advertised
// device to Serial, flagging any whose name starts with "MICRA" (how
// pylamarzocco identifies La Marzocco machines). No connection or auth — this
// just confirms the radio works and the machine is advertising/reachable, and
// tells us its exact advertised name before we build the real client.

namespace platform::ble {

void scan_and_report(int seconds);

}  // namespace platform::ble
