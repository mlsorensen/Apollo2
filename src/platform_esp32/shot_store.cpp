#include "platform_esp32/shot_store.h"

#if defined(BOARD_HAS_SD_MMC)

#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>

#include <cstring>

#include "core/shot_csv.h"
#include "driver/sdmmc_host.h"
#include "esp_heap_caps.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "sdmmc_cmd.h"

// stb PNG encoder for the on-SD shot cards. This is the DEVICE's only stb
// implementation TU (the host's lives in png_display.cpp — one per binary).
// All stb allocations go to PSRAM: the RGB888 frame + deflate buffers total
// megabytes, which would exhaust internal RAM.
#define STBIW_MALLOC(sz) heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define STBIW_REALLOC(p, newsz) \
  heap_caps_realloc(p, newsz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define STBIW_FREE(p) heap_caps_free(p)
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "vendor/stb_image_write.h"

namespace platform {

namespace {

constexpr const char* kMount = "/sdcard";
constexpr uint32_t kRetryMs = 5000;  // mount retry cadence while unavailable
constexpr int kQueueDepth = 2;

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
const char* shot_file_path(uint32_t id, const char* ext) {
  std::snprintf(g_path, sizeof(g_path), "%s/%s/shots/%06lu.%s", kMount,
                core::kShotDirName, static_cast<unsigned long>(id), ext);
  return g_path;
}

void png_write_cb(void* ctx, void* data, int size) {
  static_cast<File*>(ctx)->write(static_cast<const uint8_t*>(data),
                                 static_cast<size_t>(size));
}

// Identify a boot/volume sector's filesystem. Empty string = unrecognized.
const char* classify_fs_sector(const uint8_t* sec) {
  if (std::memcmp(sec + 3, "EXFAT   ", 8) == 0) return "exFAT";
  if (std::memcmp(sec + 3, "NTFS    ", 8) == 0) return "NTFS";
  if (std::memcmp(sec + 82, "FAT32", 5) == 0) return "FAT32";
  if (std::memcmp(sec + 54, "FAT", 3) == 0) return "FAT16";
  return "";
}

// Mount failed but is a card even in the slot — and if so, what's ON it?
// Brings the card up at the raw SDMMC protocol level (same pins/LDO as the
// mount path) and reads the boot sector, following one level of MBR
// partition table. Returns the medium state; fills fs_type for kBadFormat.
core::MediumState probe_card_format(char* fs_type, size_t n) {
  fs_type[0] = '\0';
  sd_pwr_ctrl_ldo_config_t ldo_config = {};
  ldo_config.ldo_chan_id = board::kSdLdoChannel;
  sd_pwr_ctrl_handle_t pwr = nullptr;
  if (sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &pwr) != ESP_OK)
    return core::MediumState::kNone;

  core::MediumState state = core::MediumState::kNone;
  sdmmc_card_t* card = nullptr;
  uint8_t* sec = nullptr;
  bool host_up = false;
  do {
    if (sdmmc_host_init() != ESP_OK) break;
    host_up = true;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.clk = (gpio_num_t)board::kSdClk;
    slot.cmd = (gpio_num_t)board::kSdCmd;
    slot.d0 = (gpio_num_t)board::kSdD0;
    slot.d1 = (gpio_num_t)board::kSdD1;
    slot.d2 = (gpio_num_t)board::kSdD2;
    slot.d3 = (gpio_num_t)board::kSdD3;
    slot.width = 4;
    if (sdmmc_host_init_slot(SDMMC_HOST_SLOT_1, &slot) != ESP_OK) break;

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();  // slot 1
    host.pwr_ctrl_handle = pwr;
    card = static_cast<sdmmc_card_t*>(heap_caps_malloc(
        sizeof(sdmmc_card_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    sec = static_cast<uint8_t*>(
        heap_caps_malloc(512, MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
    if (card == nullptr || sec == nullptr) break;
    if (sdmmc_card_init(&host, card) != ESP_OK) break;  // kNone: no card

    state = core::MediumState::kBadFormat;  // card is there; find out what's on it
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
  if (host_up) sdmmc_host_deinit();
  sd_pwr_ctrl_del_on_chip_ldo(pwr);
  return state;
}

}  // namespace

void ShotStore::begin() {
  mutex_ = xSemaphoreCreateMutex();
  queue_ = xQueueCreate(kQueueDepth, sizeof(SaveJob));
  // Low priority on the non-LVGL core: PNG encode is seconds of pure CPU.
  xTaskCreatePinnedToCore(task_entry, "shot_store", 16384, this, 1, nullptr, 0);
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
      heap_caps_free(job.rec);
      if (job.px != nullptr) heap_caps_free(job.px);
    } else {
      // Idle: cheap liveness probe so a yanked card flips the UI to the
      // guidance card within a few seconds instead of on the next write.
      File d = SD_MMC.open(dir_path());
      if (!d) {
        unmount();
      } else {
        d.close();
        refresh_storage();
      }
    }
  }
}

bool ShotStore::try_mount() {
  // Mount attempts are silent-ish (a card is often simply absent); log every
  // ~minute so a present-but-unmountable card (exFAT? wiring?) explains
  // itself on serial instead of looking like "nothing happens".
  static uint32_t attempts = 0;
  const bool log_this = (attempts++ % 12) == 0;
  SD_MMC.setPins(board::kSdClk, board::kSdCmd, board::kSdD0, board::kSdD1,
                 board::kSdD2, board::kSdD3);
#ifdef SOC_SDMMC_IO_POWER_EXTERNAL
  // The SD pads' IO rail is on-chip LDO-powered on the P4 (BSP ldo_chan_id 4)
  // — without this every mount fails exactly as if no card were inserted.
  SD_MMC.setPowerChannel(board::kSdLdoChannel);
#endif
  if (!SD_MMC.begin(kMount, /*mode1bit=*/false)) {
    SD_MMC.end();
    // Tell "empty slot" apart from "card we can't read": probe the card raw
    // and read its boot sector. The UI turns kBadFormat into a "reformat as
    // FAT32" instruction naming the actual filesystem.
    core::StorageInfo info{};
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
        Serial.println("ShotStore: no SD card detected");
    }
    return false;
  }
  if (SD_MMC.cardType() == CARD_NONE) {
    if (log_this) Serial.println("ShotStore: no SD card detected");
    SD_MMC.end();
    xSemaphoreTake(mutex_, portMAX_DELAY);
    storage_info_ = {};
    xSemaphoreGive(mutex_);
    return false;
  }
  SD_MMC.mkdir(dir_path());
  SD_MMC.mkdir(shots_dir_path());

  // Load the index into RAM (newest first). Also derives the next id.
  std::vector<core::ShotSummary> loaded;
  uint32_t max_id = 0;
  File f = SD_MMC.open(index_path(), FILE_READ);
  if (f) {
    // Rows are ~60 bytes; hundreds of shots parse in a blink.
    String line;
    while (f.available()) {
      line = f.readStringUntil('\n');
      core::ShotSummary s;
      if (core::parse_shot_index_row(line.c_str(), s)) {
        loaded.insert(loaded.begin(), s);  // file is oldest-first
        if (s.id > max_id) max_id = s.id;
      }
    }
    f.close();
  } else {
    File h = SD_MMC.open(index_path(), FILE_WRITE);
    if (!h) {  // can't even create the index -> treat as unusable
      SD_MMC.end();
      return false;
    }
    h.print(core::kShotIndexHeader);
    h.close();
  }

  xSemaphoreTake(mutex_, portMAX_DELAY);
  index_ = std::move(loaded);
  next_id_ = max_id + 1;
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
  SD_MMC.end();
  Serial.println("ShotStore: SD unavailable (removed?), will retry");
}

void ShotStore::write_job(SaveJob& job) {
  if (!available_) return;
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
  File idx = SD_MMC.open(index_path(), FILE_APPEND);
  if (!idx) {
    unmount();
    return;
  }
  idx.print(row);
  idx.close();

  // Samples CSV.
  File sf = SD_MMC.open(shot_file_path(r.summary.id, "csv"), FILE_WRITE);
  if (!sf) {
    unmount();
    return;
  }
  sf.print(core::kShotSamplesHeader);
  for (int i = 0; i < r.n_samples; ++i) {
    core::format_shot_sample_row(row, sizeof(row), r.samples[i]);
    sf.print(row);
  }
  sf.close();

  // PNG card: RGB565 -> RGB888 in PSRAM, then stb encodes (seconds — that's
  // why this whole path lives on the writer task).
  if (job.px != nullptr) {
    const size_t npix = static_cast<size_t>(job.w) * job.h;
    uint8_t* rgb = static_cast<uint8_t*>(
        heap_caps_malloc(npix * 3, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (rgb != nullptr) {
      for (size_t i = 0; i < npix; ++i) {
        const uint16_t p = job.px[i];
        rgb[i * 3 + 0] = static_cast<uint8_t>(((p >> 11) & 0x1F) * 255 / 31);
        rgb[i * 3 + 1] = static_cast<uint8_t>(((p >> 5) & 0x3F) * 255 / 63);
        rgb[i * 3 + 2] = static_cast<uint8_t>((p & 0x1F) * 255 / 31);
      }
      File pf = SD_MMC.open(shot_file_path(r.summary.id, "png"), FILE_WRITE);
      if (pf) {
        stbi_write_png_to_func(png_write_cb, &pf, job.w, job.h, 3, rgb,
                               job.w * 3);
        pf.close();
      }
      heap_caps_free(rgb);
    }
  }
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
  rec->card_rgb565 = nullptr;  // pixels travel separately (packed copy below)

  SaveJob job{rec, nullptr, record.card_w, record.card_h};
  if (record.card_rgb565 != nullptr && record.card_w > 0 && record.card_h > 0) {
    const size_t npix = static_cast<size_t>(record.card_w) * record.card_h;
    job.px = static_cast<uint16_t*>(
        heap_caps_malloc(npix * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (job.px != nullptr) {
      // Pack stride-padded rows tight.
      for (int y = 0; y < record.card_h; ++y)
        std::memcpy(job.px + static_cast<size_t>(y) * record.card_w,
                    record.card_rgb565 + static_cast<size_t>(y) * record.card_stride_px,
                    static_cast<size_t>(record.card_w) * 2);
    }
  }
  if (xQueueSend(queue_, &job, 0) != pdTRUE) {
    // Queue full (two shots mid-write?) — drop rather than block the UI.
    heap_caps_free(rec);
    if (job.px != nullptr) heap_caps_free(job.px);
    Serial.println("ShotStore: save queue full, shot dropped");
  }
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
  out.card_rgb565 = nullptr;
  char path[64];
  std::snprintf(path, sizeof(path), "%s/%s/shots/%06lu.csv", kMount,
                core::kShotDirName, static_cast<unsigned long>(id));
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) return true;  // summary alone still opens the card (empty graph)
  while (f.available() && out.n_samples < core::ShotRecord::kSampleCap) {
    const String line = f.readStringUntil('\n');
    core::ShotSample s;
    if (core::parse_shot_sample_row(line.c_str(), s))
      out.samples[out.n_samples++] = s;
  }
  f.close();
  return true;
}

void ShotStore::refresh_storage() {
  const uint64_t total = SD_MMC.totalBytes();
  const uint64_t used = SD_MMC.usedBytes();
  const uint64_t free_b = total > used ? total - used : 0;
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

core::ShotStats ShotStore::stats(int64_t now_unix) const {
  if (!available_) return {};
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const core::ShotStats st = core::compute_shot_stats(
      index_.data(), static_cast<int>(index_.size()), now_unix);
  xSemaphoreGive(mutex_);
  return st;
}

}  // namespace platform

#endif  // BOARD_HAS_SD_MMC
