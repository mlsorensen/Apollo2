#pragma once

#include "platform_esp32/board_config.h"

#if defined(BOARD_HAS_SD_MMC)

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include <string>
#include <vector>

#include "core/settings_backup.h"
#include "core/shot_store.h"

namespace platform {

// SD-card shot history. Layout under /Apollo2 (see core/shot_csv.h): an
// index CSV parsed into RAM at mount plus per-shot samples CSVs — a plain
// take-away database readable anywhere (graphs are re-rendered from the data
// by every viewer; nothing is pre-rendered).
//
// Threading: list/count/stats/read run on the LVGL thread against the RAM
// index (mutex-guarded). save() deep-copies into PSRAM and enqueues; a
// background writer task owns ALL SD I/O — index appends, samples, and any
// mount after boot. Any I/O failure unmounts.
//
// MOUNT POLICY (v1.0.3): the slot is probed ONCE, synchronously, at boot —
// mount_early(), called BEFORE the hosted radio link comes up — and after
// that only when the user taps Mount (a job on the writer task). There is
// no retry loop. Why: on the P4 the SD slot (SDMMC slot 0) and the C6 radio
// (slot 1) share one controller with ONE chip-wide clock divider; a cardless
// probe needs the 400 kHz enumeration clock (host_div 10) while the C6 runs
// at 40 MHz (host_div 4), and the IDF driver rewrites that shared divider
// around every command for the probe's duration. Done every 5 s under live
// radio traffic, the C6 link eventually read a corrupted frame length and
// the hosted driver asserted (sdio_rx_get_buffer; owner's X-8, 2026-10-16,
// coredump + memfail record) or silently restarted the host. A MOUNTED card
// runs at 40 MHz = the C6's divider, so its 5 s liveness ping (CMD13, kept)
// never changes the clock — the P4-5 with a card never crashed. A pulled
// card is noticed by that ping and left unmounted (kRemoved) until Mount.
// The settings backup rides along on this class rather than owning storage of
// its own: the writer task and its mount ARE the medium (all the retry/no-op-
// deinit policy above), and a settings file is one more thing on the same card.
// The UI only ever sees the two ports.
class ShotStore : public core::IShotStore, public core::ISettingsBackup {
 public:
  // The one automatic mount: probe the slot and mount a card if present,
  // synchronously on the caller. Call BEFORE the hosted radio link is brought
  // up (see the mount policy above); safe with an empty slot (~30 ms).
  void mount_early();
  // Spawns the writer task. Call once after NVS/Serial are up.
  void begin();

  bool available() const override { return available_; }
  void save(const core::ShotRecord& record) override;
  int count() const override;
  int list(core::ShotSummary* out, int max, int offset) const override;
  bool read(uint32_t id, core::ShotRecord& out) const override;
  // Drops the shot from the RAM index immediately (the UI refreshes off it);
  // the samples-file delete + index rewrite run on the writer task.
  bool remove(uint32_t id) override;
  core::ShotStats stats(int64_t now_unix) const override;
  // Cached by the writer task (mount, idle probe, every save) — FATFS free
  // -space queries aren't safe alongside a concurrent write, so the UI
  // thread only ever reads this cache.
  core::StorageInfo storage() const override;
  bool supports_mount() const override { return true; }
  void request_mount() override;    // queue a mount attempt (writer task)
  void request_unmount() override;  // queue a flush + unmount (writer task)
  // Stats-reset marker: lives on the card (/Apollo2/stats_since.txt) so it
  // travels with the data; cached here, written by the writer task.
  int64_t stats_since() const override;
  void set_stats_since(int64_t t) override;

  // --- core::ISettingsBackup ----------------------------------------------
  // Cached at mount and after every backup/restore (the UI thread must never
  // touch the card); the requests queue a job for the writer task.
  core::BackupInfo info() const override;
  void request_backup(bool include_wifi, bool include_token) override;
  void request_restore() override;
  core::BackupState state() const override { return backup_state_; }
  core::BackupOp last_op() const override { return backup_op_; }
  std::string message() const override;

  // Below this free space, saves are dropped (a shot's files run ~200-400 KB
  // and FS writes fail silently once space runs out).
  static constexpr uint64_t kMinFreeBytes = 2ull * 1024 * 1024;

 private:
  enum class JobKind : uint8_t { kShot, kBackup, kRestore, kMount, kUnmount };

  struct SaveJob {
    core::ShotRecord* rec;  // PSRAM copy, owned by the writer. null jobs:
    uint32_t remove_id;     //   0 = stats-reset marker (persist stats_since_),
                            //   else = delete this shot's files off the card
    JobKind kind;           // kShot: the above. Otherwise a settings job.
  };

  static void task_entry(void* self);
  void run();                 // writer task loop
  bool try_mount();           // mount + load index; true if usable
  void unmount(core::MediumState why);  // drop the card; `why` is what the UI shows
  void set_medium_state(core::MediumState st, bool busy);  // cache update (any task)
  void write_job(SaveJob& job);
  void remove_files(uint32_t id);  // writer task only: samples + index rewrite
  void refresh_storage();     // writer task only: requery + cache capacity
  void settings_job(const SaveJob& job);  // writer task only: backup / restore
  void refresh_backup_info();  // writer task only: re-read the file's header

  void* card_ = nullptr;  // sdmmc_card_t* while mounted (IDF type kept out
                          // of this header)

  volatile bool available_ = false;
  uint32_t next_id_ = 1;
  int64_t stats_since_ = 0;               // cache; guarded by mutex_
  std::vector<core::ShotSummary> index_;  // newest first
  core::StorageInfo storage_info_;        // cache; guarded by mutex_
  core::BackupInfo backup_info_;          // cache; guarded by mutex_
  std::string backup_msg_;                // cache; guarded by mutex_
  volatile core::BackupState backup_state_ = core::BackupState::kIdle;
  volatile core::BackupOp backup_op_ = core::BackupOp::kNone;
  bool backup_wifi_ = true;   // request parameters, set before the job is
  bool backup_token_ = true;  // queued (one request can be in flight)
  mutable SemaphoreHandle_t mutex_ = nullptr;
  QueueHandle_t queue_ = nullptr;
};

}  // namespace platform

#endif  // BOARD_HAS_SD_MMC
