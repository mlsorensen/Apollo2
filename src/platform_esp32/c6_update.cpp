#include "platform_esp32/c6_update.h"

#include <Arduino.h>  // pulls sdkconfig.h -> CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE

#include "core/system.h"

#if defined(CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE)
#include <Preferences.h>
#include <esp32-hal-hosted.h>

// Embedded matching slave image (platformio.ini board_build.embed_files ->
// firmware_blobs/c6_slave.bin). PlatformIO/IDF names the symbol from the file
// PATH with '/'/'.'/'-' turned to '_' — hence the firmware_blobs_ prefix.
extern const uint8_t c6_fw_start[] asm("_binary_firmware_blobs_c6_slave_bin_start");
extern const uint8_t c6_fw_end[] asm("_binary_firmware_blobs_c6_slave_bin_end");

namespace {
constexpr char kNamespace[] = "micra";     // shared with Config
constexpr char kTriesKey[] = "c6try";       // consecutive failed-boot counter
constexpr int kMaxTries = 3;  // give up after 3 failed boots (don't loop forever)

int read_tries() {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/true)) return 0;
  const int v = p.isKey(kTriesKey) ? p.getInt(kTriesKey, 0) : 0;
  p.end();
  return v;
}
void write_tries(int v) {
  Preferences p;
  if (!p.begin(kNamespace, /*readOnly=*/false)) return;
  p.putInt(kTriesKey, v);
  p.end();
}
}  // namespace
#endif

namespace platform {

bool c6_update_needed() {
#if defined(CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE)
  // hostedHasUpdate(): host lib version > slave version (a stale slave that
  // can't answer the RPC reads as version 0, so this is true).
  if (!hostedHasUpdate()) {
    if (read_tries() != 0) write_tries(0);  // slave is current now; clear guard
    return false;
  }
  if (read_tries() >= kMaxTries) {
    core::logf("C6 update: giving up after %d tries; staying on old slave\n",
               kMaxTries);
    return false;
  }
  return true;
#else
  return false;
#endif
}

bool c6_update_apply(const std::function<void(int)>& progress) {
#if defined(CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE)
  const uint32_t total = static_cast<uint32_t>(c6_fw_end - c6_fw_start);
  if (total < 1024) {
    core::logf("C6 update: embedded image missing/too small (%u)\n",
               static_cast<unsigned>(total));
    return false;
  }
  // Count this attempt BEFORE touching the slave: if the write bricks the link
  // and we reboot, the guard still advances so we can't loop forever.
  write_tries(read_tries() + 1);

  core::logf("C6 update: streaming %u bytes to co-processor\n",
             static_cast<unsigned>(total));
  if (!hostedBeginUpdate()) {
    core::logf("C6 update: begin failed\n");
    return false;
  }
  // begin() tells the C6 to ERASE its OTA partition — that takes a few seconds.
  // Espressif's download-based updater survives this only because it waits for
  // the first TCP bytes before the first write; feeding an embedded image with
  // no such pause fires OTAWrite into a still-erasing slave and its 5 s RPC
  // timeout trips. Wait it out here before the first write.
  core::logf("C6 update: waiting for co-processor erase...\n");
  delay(6000);
  // Match Espressif's proven 2 KB chunking (HOSTED_OTA_BUF_SIZE).
  constexpr uint32_t kChunk = 2048;
  uint32_t off = 0;
  int last_pct = -1;
  while (off < total) {
    const uint32_t n = (total - off < kChunk) ? (total - off) : kChunk;
    if (!hostedWriteUpdate(const_cast<uint8_t*>(c6_fw_start + off), n)) {
      core::logf("C6 update: write failed at %u/%u\n",
                 static_cast<unsigned>(off), static_cast<unsigned>(total));
      return false;
    }
    off += n;
    const int pct = static_cast<int>(off * 100 / total);
    if (pct != last_pct) {
      last_pct = pct;
      if (progress) progress(pct);
    }
  }
  if (!hostedEndUpdate()) {
    core::logf("C6 update: end/verify failed\n");
    return false;
  }
  core::logf("C6 update: activating new slave firmware\n");
  hostedActivateUpdate();  // reboots the C6; may report failure on very old
                           // slaves yet still apply (per the Arduino HAL)
  write_tries(0);          // applied -> clear the guard for the next boot
  return true;
#else
  (void)progress;
  return false;
#endif
}

}  // namespace platform
