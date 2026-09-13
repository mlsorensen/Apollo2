#pragma once

// Failed-allocation flight recorder. A core dump on this platform holds task
// stacks only (no heap), so when an allocation fails — the moment before the
// hosted radio's `sdio_rx_get_buffer` assert, or a TLS buffer coming up short —
// nothing records what the pool looked like. This hooks the heap's
// failed-allocation callback (a runtime IDF API, no sdkconfig change), takes a
// census of the pool that refused the request (totals, used blocks by exact
// size, the largest free holes) and parks it in RTC memory, which survives a
// panic reboot. The next boot prints it into the log ring. Costs ~300 bytes of
// RTC/LP memory and no heap; nothing runs unless an allocation actually fails.
namespace platform::crash_record {

void install();         // register the hook; call right after log_init()
void report_at_boot();  // print + clear a record left by the previous boot
void self_test();       // dev: force one impossible allocation to exercise it

}  // namespace platform::crash_record
