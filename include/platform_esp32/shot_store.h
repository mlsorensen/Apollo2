#pragma once

#include "platform_esp32/board_config.h"

#if defined(BOARD_HAS_SD_MMC)

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include <vector>

#include "core/shot_store.h"

namespace platform {

// SD-card shot history (4-bit SD_MMC). Layout under /Apollo2 (see
// core/shot_csv.h): an index CSV parsed into RAM at mount, per-shot samples
// CSVs, and a PNG card per shot — a take-away database readable anywhere.
//
// Threading: list/count/stats/read run on the LVGL thread against the RAM
// index (mutex-guarded). save() deep-copies into PSRAM and enqueues; a
// background writer task owns ALL SD I/O — mounting (lazy, retried), index
// appends, samples, and the PNG encode (RGB565->RGB888 + stb, seconds of CPU
// — never on the UI thread). Any I/O failure unmounts; the card can be
// hot-inserted/removed and the store recovers on the next retry tick.
class ShotStore : public core::IShotStore {
 public:
  // Spawns the writer task. Call once after NVS/Serial are up; safe before
  // any card is inserted.
  void begin();

  bool available() const override { return available_; }
  void save(const core::ShotRecord& record) override;
  int count() const override;
  int list(core::ShotSummary* out, int max, int offset) const override;
  bool read(uint32_t id, core::ShotRecord& out) const override;
  core::ShotStats stats(int64_t now_unix) const override;

 private:
  struct SaveJob {
    core::ShotRecord* rec;  // PSRAM copy, owned by the writer
    uint16_t* px;           // packed RGB565 card, PSRAM, may be null
    int w, h;
  };

  static void task_entry(void* self);
  void run();                 // writer task loop
  bool try_mount();           // mount + load index; true if usable
  void unmount();             // failure path: drop the card, retry later
  void write_job(SaveJob& job);

  volatile bool available_ = false;
  uint32_t next_id_ = 1;
  std::vector<core::ShotSummary> index_;  // newest first
  mutable SemaphoreHandle_t mutex_ = nullptr;
  QueueHandle_t queue_ = nullptr;
};

}  // namespace platform

#endif  // BOARD_HAS_SD_MMC
