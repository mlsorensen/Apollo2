#include "platform_host/dir_shot_store.h"

#include <cstdio>
#include <filesystem>

#include "core/shot_csv.h"
#include "vendor/stb_image_write.h"  // impl TU lives in png_display.cpp

namespace host {

namespace fs = std::filesystem;

DirShotStore::DirShotStore(std::string root) {
  dir_ = root + "/" + core::kShotDirName;
  fs::create_directories(dir_ + "/shots");
  const std::string index = dir_ + "/shots.csv";
  if (FILE* f = std::fopen(index.c_str(), "r")) {
    char line[160];
    while (std::fgets(line, sizeof(line), f) != nullptr) {
      core::ShotSummary s;
      if (core::parse_shot_index_row(line, s)) {
        index_.insert(index_.begin(), s);  // file is oldest-first
        if (s.id >= next_id_) next_id_ = s.id + 1;
      }
    }
    std::fclose(f);
  } else if (FILE* h = std::fopen(index.c_str(), "w")) {
    std::fputs(core::kShotIndexHeader, h);
    std::fclose(h);
  }
  if (FILE* m = std::fopen((dir_ + "/stats_since.txt").c_str(), "r")) {
    long long v = 0;
    if (std::fscanf(m, "%lld", &v) == 1) stats_since_ = v;
    std::fclose(m);
  }
}

void DirShotStore::set_stats_since(int64_t t) {
  stats_since_ = t;
  if (FILE* m = std::fopen((dir_ + "/stats_since.txt").c_str(), "w")) {
    std::fprintf(m, "%lld\n", static_cast<long long>(t));
    std::fclose(m);
  }
}

void DirShotStore::save(const core::ShotRecord& record) {
  core::ShotSummary s = record.summary;
  s.id = next_id_++;
  index_.insert(index_.begin(), s);

  char row[160], path[256];
  core::format_shot_index_row(row, sizeof(row), s);
  const std::string index = dir_ + "/shots.csv";
  if (FILE* f = std::fopen(index.c_str(), "a")) {
    std::fputs(row, f);
    std::fclose(f);
  }

  std::snprintf(path, sizeof(path), "%s/shots/%06u.csv", dir_.c_str(), s.id);
  if (FILE* f = std::fopen(path, "w")) {
    std::fputs(core::kShotSamplesHeader, f);
    for (int i = 0; i < record.n_samples; ++i) {
      core::format_shot_sample_row(row, sizeof(row), record.samples[i]);
      std::fputs(row, f);
    }
    std::fclose(f);
  }

  if (record.card_rgb565 != nullptr && record.card_w > 0 && record.card_h > 0) {
    const int w = record.card_w, h = record.card_h;
    std::vector<unsigned char> rgb(static_cast<size_t>(w) * h * 3);
    for (int y = 0; y < h; ++y) {
      const uint16_t* src =
          record.card_rgb565 + static_cast<size_t>(y) * record.card_stride_px;
      for (int x = 0; x < w; ++x) {
        const uint16_t p = src[x];
        const size_t o = (static_cast<size_t>(y) * w + x) * 3;
        rgb[o + 0] = static_cast<unsigned char>(((p >> 11) & 0x1F) * 255 / 31);
        rgb[o + 1] = static_cast<unsigned char>(((p >> 5) & 0x3F) * 255 / 63);
        rgb[o + 2] = static_cast<unsigned char>((p & 0x1F) * 255 / 31);
      }
    }
    std::snprintf(path, sizeof(path), "%s/shots/%06u.png", dir_.c_str(), s.id);
    stbi_write_png(path, w, h, 3, rgb.data(), w * 3);
  }
}

int DirShotStore::list(core::ShotSummary* out, int max, int offset) const {
  int written = 0;
  for (size_t i = offset; i < index_.size() && written < max; ++i)
    out[written++] = index_[i];
  return written;
}

bool DirShotStore::read(uint32_t id, core::ShotRecord& out) const {
  for (const auto& s : index_) {
    if (s.id != id) continue;
    out.summary = s;
    out.n_samples = 0;
    out.card_rgb565 = nullptr;
    char path[256];
    std::snprintf(path, sizeof(path), "%s/shots/%06u.csv", dir_.c_str(), id);
    if (FILE* f = std::fopen(path, "r")) {
      char line[80];
      while (std::fgets(line, sizeof(line), f) != nullptr &&
             out.n_samples < core::ShotRecord::kSampleCap) {
        core::ShotSample sample;
        if (core::parse_shot_sample_row(line, sample))
          out.samples[out.n_samples++] = sample;
      }
      std::fclose(f);
    }
    return true;
  }
  return false;
}

core::ShotStats DirShotStore::stats(int64_t now_unix) const {
  return core::compute_shot_stats(index_.data(), static_cast<int>(index_.size()),
                                  now_unix, stats_since_);
}

core::StorageInfo DirShotStore::storage() const {
  std::error_code ec;
  const fs::space_info sp = fs::space(dir_, ec);
  if (ec) return {};
  return {sp.capacity, sp.available, sp.available < 2ull * 1024 * 1024};
}

}  // namespace host
