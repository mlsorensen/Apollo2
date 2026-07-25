#include "platform_esp32/shot_store.h"

#if defined(BOARD_HAS_SD_MMC)

#include <Arduino.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

#include "core/shot_csv.h"
#include "driver/sdmmc_host.h"
#include "esp_heap_caps.h"
#include "esp_vfs_fat.h"
#if !defined(BOARD_SD_MMC_1BIT)
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#endif
#include "sdmmc_cmd.h"

// The card is mounted the way the Waveshare BSPs do it, via IDF's
// esp_vfs_fat_sdmmc_mount; file IO goes through plain stdio on the VFS
// mountpoint. Two wirings share this file:
//   - P4 boards: SDMMC SLOT 0, 4-bit (IOMUX — the card is hard-wired to
//     GPIO39-44), the slot's IO rail powered by on-chip LDO channel 4.
//     Arduino's SD_MMC library is NOT usable there: it hardcodes slot 1,
//     which the ESP32-C6 radio occupies (esp-hosted SDIO).
//   - S3 4.3C (BOARD_SD_MMC_1BIT): SLOT 1 via the GPIO matrix, 1-bit bus
//     (vendor demo pins CLK 12 / CMD 11 / D0 13), no LDO involved.

namespace platform {

namespace {

constexpr const char* kMount = "/sdcard";
constexpr uint32_t kRetryMs = 5000;  // mount retry cadence while unavailable
constexpr int kQueueDepth = 2;

#if defined(BOARD_SD_MMC_1BIT)
constexpr int kSdSlot = SDMMC_HOST_SLOT_1;  // GPIO matrix: either slot works
#else
constexpr int kSdSlot = SDMMC_HOST_SLOT_0;  // the P4 card's IOMUX slot
#endif

char g_path[64];  // writer-task scratch (single-threaded there)

const char* dir_path() {
  std::snprintf(g_path, sizeof(g_path), "%s/%s", kMount, core::kShotDirName);
  return g_path;
}
const char* index_path() {
  std::snprintf(g_path, sizeof(g_path), "%s/%s/shots.csv", kMount,
                core::kShotDirName);
  return g_path;
}
const char* shots_dir_path() {
  std::snprintf(g_path, sizeof(g_path), "%s/%s/shots", kMount,
                core::kShotDirName);
  return g_path;
}
const char* stats_since_path() {
  std::snprintf(g_path, sizeof(g_path), "%s/%s/stats_since.txt", kMount,
                core::kShotDirName);
  return g_path;
}
const char* shot_file_path(uint32_t id, const char* ext) {
  std::snprintf(g_path, sizeof(g_path), "%s/%s/shots/%06lu.%s", kMount,
                core::kShotDirName, static_cast<unsigned long>(id), ext);
  return g_path;
}

// Per-board slot config. P4: slot 0 is IOMUX, so no pins are specified
// (the BSP's config verbatim). 4.3C: pins routed through the GPIO matrix,
// one data line.
sdmmc_slot_config_t sd_slot_config() {
#if defined(BOARD_SD_MMC_1BIT)
  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.clk = (gpio_num_t)board::kSdClk;
  slot.cmd = (gpio_num_t)board::kSdCmd;
  slot.d0 = (gpio_num_t)board::kSdD0;
  slot.d1 = GPIO_NUM_NC;
  slot.d2 = GPIO_NUM_NC;
  slot.d3 = GPIO_NUM_NC;
  slot.cd = SDMMC_SLOT_NO_CD;
  slot.wp = SDMMC_SLOT_NO_WP;
  slot.width = 1;
  return slot;
#else
  sdmmc_slot_config_t slot = {};
  slot.cd = SDMMC_SLOT_NO_CD;
  slot.wp = SDMMC_SLOT_NO_WP;
  slot.width = 4;
  slot.flags = 0;
  return slot;
#endif
}

// Per-board host config: shared SDMMC peripheral settings for mount + probe.
sdmmc_host_t sd_host_config() {
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.slot = kSdSlot;
#if defined(BOARD_SD_MMC_1BIT)
  host.flags = SDMMC_HOST_FLAG_1BIT;  // default 20 MHz — matrix routing
#else
  host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
#endif
  return host;
}

// Identify a boot/volume sector's filesystem. Empty string = unrecognized.
const char* classify_fs_sector(const uint8_t* sec) {
  if (std::memcmp(sec + 3, "EXFAT   ", 8) == 0) return "exFAT";
  if (std::memcmp(sec + 3, "NTFS    ", 8) == 0) return "NTFS";
  if (std::memcmp(sec + 82, "FAT32", 5) == 0) return "FAT32";
  if (std::memcmp(sec + 54, "FAT", 3) == 0) return "FAT16";
  return "";
}

// Bring the SDMMC bus up ONCE and keep it up forever. On the P4 the C6
// radio shares the SDMMC peripheral (esp-hosted SDIO): letting
// esp_vfs_fat_sdmmc_mount's failure path deinit the host killed the radio
// mid-flight (sdio_rx assert, boot loop) on any board without a card
// inserted. With the host + slot + power pre-initialized by us, the mount
// helper sees "already initialized" and its cleanup leaves the host alone —
// a cardless retry loop touches only our slot. (The S3 4.3C has no sharing
// concern; the same policy is simply harmless there.)
#if !defined(BOARD_SD_MMC_1BIT)
sd_pwr_ctrl_handle_t g_pwr = nullptr;  // persistent LDO handle (never freed)
#endif

bool ensure_bus() {
  static bool ready = false;
  static bool failed = false;
  if (ready || failed) return ready;
#if !defined(BOARD_SD_MMC_1BIT)
  sd_pwr_ctrl_ldo_config_t ldo_config = {};
  ldo_config.ldo_chan_id = board::kSdLdoChannel;
  if (sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &g_pwr) != ESP_OK) {
    Serial.println("ShotStore: SD LDO power init failed");
    failed = true;
    return false;
  }
#endif
  const esp_err_t host_err = sdmmc_host_init();
  if (host_err != ESP_OK && host_err != ESP_ERR_INVALID_STATE) {
    Serial.printf("ShotStore: sdmmc host init failed (%s)\n",
                  esp_err_to_name(host_err));
    failed = true;
    return false;
  }
  const sdmmc_slot_config_t slot = sd_slot_config();
  const esp_err_t slot_err = sdmmc_host_init_slot(kSdSlot, &slot);
  if (slot_err != ESP_OK && slot_err != ESP_ERR_INVALID_STATE) {
    Serial.printf("ShotStore: sdmmc slot init failed (%s)\n",
                  esp_err_to_name(slot_err));
    failed = true;
    return false;
  }
  ready = true;
  return true;
}

// The FS wouldn't mount, but the card DID respond at the protocol level:
// bring it up raw on slot 0 and read the boot sector (following one level of
// MBR partition table) so the UI can name what's on it. Uses the persistent
// bus — inits/deinits nothing.
core::MediumState probe_card_format(char* fs_type, size_t n) {
  fs_type[0] = '\0';
  core::MediumState state = core::MediumState::kNone;
  sdmmc_card_t* card = nullptr;
  uint8_t* sec = nullptr;
  do {
    sdmmc_host_t host = sd_host_config();
#if !defined(BOARD_SD_MMC_1BIT)
    host.pwr_ctrl_handle = g_pwr;
#endif
    card = static_cast<sdmmc_card_t*>(heap_caps_malloc(
        sizeof(sdmmc_card_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    sec = static_cast<uint8_t*>(
        heap_caps_malloc(512, MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
    if (card == nullptr || sec == nullptr) break;
    if (sdmmc_card_init(&host, card) != ESP_OK) break;  // kNone: no card

    state = core::MediumState::kBadFormat;  // card responds; what's on it?
    std::snprintf(fs_type, n, "?");
    if (sdmmc_read_sectors(card, sec, 0, 1) != ESP_OK) break;
    const char* fs = classify_fs_sector(sec);
    if (fs[0] == '\0' && sec[510] == 0x55 && sec[511] == 0xAA) {
      // MBR: follow the first partition entry (type 0xEE = GPT protective).
      const uint8_t ptype = sec[0x1BE + 4];
      const uint32_t lba = static_cast<uint32_t>(sec[0x1BE + 8]) |
                           (static_cast<uint32_t>(sec[0x1BE + 9]) << 8) |
                           (static_cast<uint32_t>(sec[0x1BE + 10]) << 16) |
                           (static_cast<uint32_t>(sec[0x1BE + 11]) << 24);
      if (ptype == 0xEE) {
        fs = "GPT";
      } else if (lba != 0 && sdmmc_read_sectors(card, sec, lba, 1) == ESP_OK) {
        fs = classify_fs_sector(sec);
      }
    }
    if (fs[0] != '\0') std::snprintf(fs_type, n, "%s", fs);
  } while (false);

  if (sec != nullptr) heap_caps_free(sec);
  if (card != nullptr) heap_caps_free(card);
  return state;
}

}  // namespace

void ShotStore::begin() {
  mutex_ = xSemaphoreCreateMutex();
  queue_ = xQueueCreate(kQueueDepth, sizeof(SaveJob));
  // Low priority on the non-LVGL core. Stack is INTERNAL RAM (the radios
  // and DMA compete for it — scarcest on the S3 boards): 8 KB covers FATFS +
  // stdio, and the create is CHECKED because a silent failure here means
  // shots quietly never save.
  if (xTaskCreatePinnedToCore(task_entry, "shot_store", 8192, this, 1, nullptr,
                              0) != pdPASS) {
    Serial.println("ShotStore: FAILED to create writer task (internal RAM?) — "
                   "shots will not be saved");
  }
}

void ShotStore::task_entry(void* self) { static_cast<ShotStore*>(self)->run(); }

void ShotStore::run() {
  for (;;) {
    if (!available_) {
      if (!try_mount()) {
        vTaskDelay(pdMS_TO_TICKS(kRetryMs));
        continue;
      }
    }
    SaveJob job;
    if (xQueueReceive(queue_, &job, pdMS_TO_TICKS(kRetryMs)) == pdTRUE) {
      write_job(job);
      if (job.rec != nullptr) heap_caps_free(job.rec);
    } else {
      // Idle: liveness probe so a yanked card flips the UI to the guidance
      // card within a few seconds instead of on the next write. This must go
      // to the CARD, not the filesystem — FATFS serves repeated metadata
      // lookups (stat etc.) from its sector cache without touching the bus,
      // which is exactly how a removal stays invisible. CMD13 can't lie.
      if (sdmmc_get_status(static_cast<sdmmc_card_t*>(card_)) != ESP_OK) {
        unmount();
      } else {
        refresh_storage();
      }
    }
  }
}

bool ShotStore::try_mount() {
  // Mount attempts are frequent while no card is inserted; log once a minute
  // so a present-but-unmountable card explains itself on serial too (the UI
  // gets the probe verdict either way).
  static uint32_t attempts = 0;
  const bool log_this = (attempts++ % 12) == 0;

  // BSP-style mount on the persistent bus (see sd_host_config/sd_slot_config
  // for the per-board wiring). The host is pre-initialized (ensure_bus), so
  // the mount helper's failure cleanup will NOT deinit it — that used to
  // take the P4's C6 radio down with it.
  if (!ensure_bus()) return false;
  sdmmc_host_t host = sd_host_config();
#if !defined(BOARD_SD_MMC_1BIT)
  host.pwr_ctrl_handle = g_pwr;
#endif
  const sdmmc_slot_config_t slot_config = sd_slot_config();
  esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
  mount_config.format_if_mount_failed = false;
  mount_config.max_files = 4;
  mount_config.allocation_unit_size = 64 * 1024;

  sdmmc_card_t* card = nullptr;
  const esp_err_t err =
      esp_vfs_fat_sdmmc_mount(kMount, &host, &slot_config, &mount_config, &card);
  if (err != ESP_OK) {
    // ESP_FAIL = the card answered but the filesystem didn't mount — probe
    // what's actually on it so the UI can say "reformat as FAT32".
    core::StorageInfo info{};
    if (err == ESP_FAIL)
      info.state = probe_card_format(info.fs_type, sizeof(info.fs_type));
    xSemaphoreTake(mutex_, portMAX_DELAY);
    storage_info_ = info;
    xSemaphoreGive(mutex_);
    if (log_this) {
      if (info.state == core::MediumState::kBadFormat)
        Serial.printf(
            "ShotStore: SD card present but %s-formatted — reformat as FAT32\n",
            info.fs_type);
      else
        Serial.printf("ShotStore: SD mount failed (%s)\n", esp_err_to_name(err));
    }
    return false;
  }

  mkdir(dir_path(), 0775);
  mkdir(shots_dir_path(), 0775);

  // Load the index into RAM (newest first). Also derives the next id.
  std::vector<core::ShotSummary> loaded;
  uint32_t max_id = 0;
  FILE* f = std::fopen(index_path(), "r");
  if (f != nullptr) {
    // The header line is the format version. On a mismatch (index written by
    // NEWER firmware) leave the file strictly alone — appending v1 rows into
    // a future format, or sscanf-guessing at reordered columns, would
    // corrupt the user's database silently.
    char line[160];
    if (std::fgets(line, sizeof(line), f) != nullptr &&
        std::strcmp(line, core::kShotIndexHeader) != 0) {
      std::fclose(f);
      esp_vfs_fat_sdcard_unmount(kMount, card);
      if (log_this)
        Serial.println(
            "ShotStore: shots.csv has an unknown format (newer firmware "
            "wrote it?) — not touching it");
      return false;
    }
    // Rows are ~60 bytes; hundreds of shots parse in a blink.
    while (std::fgets(line, sizeof(line), f) != nullptr) {
      core::ShotSummary s;
      if (core::parse_shot_index_row(line, s)) {
        loaded.insert(loaded.begin(), s);  // file is oldest-first
        if (s.id > max_id) max_id = s.id;
      }
    }
    std::fclose(f);
  } else {
    FILE* h = std::fopen(index_path(), "w");
    if (h == nullptr) {  // can't even create the index -> treat as unusable
      esp_vfs_fat_sdcard_unmount(kMount, card);
      if (log_this) Serial.println("ShotStore: SD mounted but not writable");
      return false;
    }
    std::fputs(core::kShotIndexHeader, h);
    std::fclose(h);
  }

  // Stats-reset marker travels with the card; absent file = never reset.
  int64_t since = 0;
  if (FILE* m = std::fopen(stats_since_path(), "r")) {
    long long v = 0;
    if (std::fscanf(m, "%lld", &v) == 1) since = v;
    std::fclose(m);
  }

  card_ = card;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  index_ = std::move(loaded);
  next_id_ = max_id + 1;
  stats_since_ = since;
  xSemaphoreGive(mutex_);
  available_ = true;
  refresh_storage();
  Serial.printf("ShotStore: SD mounted, %d shots, next id %lu\n",
                static_cast<int>(index_.size()),
                static_cast<unsigned long>(next_id_));
  return true;
}

void ShotStore::unmount() {
  available_ = false;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  storage_info_ = {};
  xSemaphoreGive(mutex_);
  if (card_ != nullptr) {
    esp_vfs_fat_sdcard_unmount(kMount, static_cast<sdmmc_card_t*>(card_));
    card_ = nullptr;
  }
  // The host, slot, and LDO stay up (shared with the radio; see ensure_bus).
  Serial.println("ShotStore: SD unavailable (removed?), will retry");
}

void ShotStore::write_job(SaveJob& job) {
  if (!available_) return;
  if (job.rec == nullptr) {  // persist the stats-reset marker
    FILE* m = std::fopen(stats_since_path(), "w");
    if (m == nullptr) {
      unmount();
      return;
    }
    std::fprintf(m, "%lld\n", static_cast<long long>(stats_since()));
    std::fclose(m);
    Serial.println("ShotStore: stats reset marker written");
    return;
  }
  // Card-full guard: FS writes fail SILENTLY when space runs out, which would
  // leave truncated CSVs and half a PNG. Skip the save (history stays
  // readable) rather than corrupt the tree; the UI shows FULL off the cache.
  refresh_storage();
  if (storage().full) {
    Serial.println("ShotStore: SD card full, shot not saved");
    return;
  }
  core::ShotRecord& r = *job.rec;

  xSemaphoreTake(mutex_, portMAX_DELAY);
  r.summary.id = next_id_++;
  index_.insert(index_.begin(), r.summary);
  xSemaphoreGive(mutex_);

  // Index row (append).
  char row[128];
  core::format_shot_index_row(row, sizeof(row), r.summary);
  FILE* idx = std::fopen(index_path(), "a");
  if (idx == nullptr) {
    unmount();
    return;
  }
  std::fputs(row, idx);
  std::fclose(idx);

  // Samples CSV.
  FILE* sf = std::fopen(shot_file_path(r.summary.id, "csv"), "w");
  if (sf == nullptr) {
    unmount();
    return;
  }
  std::fputs(core::kShotSamplesHeader, sf);
  for (int i = 0; i < r.n_samples; ++i) {
    core::format_shot_sample_row(row, sizeof(row), r.samples[i]);
    std::fputs(row, sf);
  }
  std::fclose(sf);

  Serial.printf("ShotStore: saved shot %lu (%.1fg, %lums)\n",
                static_cast<unsigned long>(r.summary.id),
                static_cast<double>(r.summary.final_g),
                static_cast<unsigned long>(r.summary.duration_ms));
}

void ShotStore::save(const core::ShotRecord& record) {
  if (!available_) return;
  auto* rec = static_cast<core::ShotRecord*>(heap_caps_malloc(
      sizeof(core::ShotRecord), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (rec == nullptr) return;
  std::memcpy(rec, &record, sizeof(core::ShotRecord));

  SaveJob job{rec};
  if (xQueueSend(queue_, &job, 0) != pdTRUE) {
    // Queue full (two shots mid-write?) — drop rather than block the UI.
    heap_caps_free(rec);
    Serial.println("ShotStore: save queue full, shot dropped");
  }
}

void ShotStore::refresh_storage() {
  uint64_t total = 0, free_b = 0;
  if (esp_vfs_fat_info(kMount, &total, &free_b) != ESP_OK) return;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  storage_info_ = {total, free_b, free_b < kMinFreeBytes,
                   core::MediumState::kOk, "FAT"};
  xSemaphoreGive(mutex_);
}

core::StorageInfo ShotStore::storage() const {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const core::StorageInfo info = storage_info_;
  xSemaphoreGive(mutex_);
  return info;
}

int ShotStore::count() const {
  if (!available_) return 0;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const int n = static_cast<int>(index_.size());
  xSemaphoreGive(mutex_);
  return n;
}

int ShotStore::list(core::ShotSummary* out, int max, int offset) const {
  if (!available_) return 0;
  int written = 0;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  for (size_t i = offset; i < index_.size() && written < max; ++i)
    out[written++] = index_[i];
  xSemaphoreGive(mutex_);
  return written;
}

bool ShotStore::read(uint32_t id, core::ShotRecord& out) const {
  if (!available_) return false;
  bool found = false;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  for (const auto& s : index_) {
    if (s.id == id) {
      out.summary = s;
      found = true;
      break;
    }
  }
  xSemaphoreGive(mutex_);
  if (!found) return false;

  // Samples come off the card (~10-20 KB — fine on the LVGL thread at
  // modal-open); mode/wired ride in the summary, parsed from the index.
  out.n_samples = 0;
  char path[64];
  std::snprintf(path, sizeof(path), "%s/%s/shots/%06lu.csv", kMount,
                core::kShotDirName, static_cast<unsigned long>(id));
  FILE* f = std::fopen(path, "r");
  if (f == nullptr) return true;  // summary alone still opens the card
  char line[80];
  while (std::fgets(line, sizeof(line), f) != nullptr &&
         out.n_samples < core::ShotRecord::kSampleCap) {
    core::ShotSample s;
    if (core::parse_shot_sample_row(line, s)) out.samples[out.n_samples++] = s;
  }
  std::fclose(f);
  return true;
}


core::ShotStats ShotStore::stats(int64_t now_unix) const {
  if (!available_) return {};
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const core::ShotStats st = core::compute_shot_stats(
      index_.data(), static_cast<int>(index_.size()), now_unix, stats_since_);
  xSemaphoreGive(mutex_);
  return st;
}

int64_t ShotStore::stats_since() const {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const int64_t t = stats_since_;
  xSemaphoreGive(mutex_);
  return t;
}

void ShotStore::set_stats_since(int64_t t) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  stats_since_ = t;
  xSemaphoreGive(mutex_);
  // Persist off-thread: a marker job is a SaveJob with no record attached.
  SaveJob job{nullptr};
  xQueueSend(queue_, &job, 0);  // queue full -> marker lost until next reset; fine
}

}  // namespace platform

#endif  // BOARD_HAS_SD_MMC
