#pragma once

#include <cstdio>
#include <cstring>
#include <ctime>

#include "core/shot_store.h"

// The on-disk shot-history format, shared by every store implementation so
// the device (Arduino File) and the host (stdio) can't drift apart:
//
//   /Apollo2/shots.csv          index, one row per shot (kShotIndexHeader)
//   /Apollo2/shots/NNNNNN.csv   per-shot samples (kShotSamplesHeader)
//   /Apollo2/shots/NNNNNN.png   rendered shot card
//
// Everything is plain CSV so the card can be pulled and analyzed on any
// computer. The index's iso8601 column is LOCAL time (what the user saw);
// unix is UTC for arithmetic.
//
// FORMAT EVOLUTION CONTRACT (the header line IS the version):
//   - The first line of each file is its header constant below. Firmware
//     parses a file ONLY when the header matches exactly; on a mismatch it
//     must leave the file untouched and surface the situation (a newer
//     format misread by sscanf would corrupt silently — never guess).
//   - Compatible changes APPEND columns, keep the same header prefix
//     semantics, and parsers default missing trailing columns (see
//     parse_shot_index_row: mode/wired are optional) and ignore extras.
//   - A breaking change (reorder/remove/retype) gets a NEW header string and
//     a migrating loader that reads the old header and rewrites the index
//     (keep the original as shots.csv.bak).

namespace core {

constexpr const char* kShotDirName = "Apollo2";
constexpr const char* kShotIndexHeader =
    "id,iso8601_local,unix,duration_ms,target_g,final_g,avg_gps,mode,wired\n";
constexpr const char* kShotSamplesHeader = "t_ms,weight_g,flow_gps\n";

inline const char* shot_mode_name(ShotMode m) {
  switch (m) {
    case ShotMode::kAuto: return "auto";
    case ShotMode::kDetect: return "detect";
    default: return "manual";
  }
}

// One index row. Returns chars written (snprintf semantics).
inline int format_shot_index_row(char* buf, size_t n, const ShotSummary& s) {
  const time_t t = static_cast<time_t>(s.unix_time);
  struct tm tm;
  localtime_r(&t, &tm);
  return std::snprintf(
      buf, n, "%lu,%04d-%02d-%02dT%02d:%02d:%02d,%lld,%lu,%.1f,%.1f,%.2f,%s,%d\n",
      static_cast<unsigned long>(s.id), tm.tm_year + 1900, tm.tm_mon + 1,
      tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
      static_cast<long long>(s.unix_time), static_cast<unsigned long>(s.duration_ms),
      static_cast<double>(s.target_g), static_cast<double>(s.final_g),
      static_cast<double>(s.avg_gps), shot_mode_name(s.mode), s.wired ? 1 : 0);
}

// Parse one index row back into a summary. Returns false on a malformed row
// (headers, blank lines).
inline bool parse_shot_index_row(const char* line, ShotSummary& out) {
  unsigned long id = 0, dur = 0;
  long long unix_time = 0;
  double target = 0, final_g = 0, avg = 0;
  char mode[16] = {};
  int wired = 0;
  // iso8601 column is skipped ("%*[^,]" — derived from unix at display time).
  const int got =
      std::sscanf(line, "%lu,%*[^,],%lld,%lu,%lf,%lf,%lf,%15[^,],%d", &id,
                  &unix_time, &dur, &target, &final_g, &avg, mode, &wired);
  if (got < 6) return false;
  out.id = static_cast<uint32_t>(id);
  out.unix_time = unix_time;
  out.duration_ms = static_cast<uint32_t>(dur);
  out.target_g = static_cast<float>(target);
  out.final_g = static_cast<float>(final_g);
  out.avg_gps = static_cast<float>(avg);
  out.mode = std::strcmp(mode, "detect") == 0 ? ShotMode::kDetect
             : std::strcmp(mode, "manual") == 0 ? ShotMode::kManual
                                                : ShotMode::kAuto;
  out.wired = wired != 0;
  return true;
}

inline int format_shot_sample_row(char* buf, size_t n, const ShotSample& s) {
  return std::snprintf(buf, n, "%lu,%.2f,%.2f\n",
                       static_cast<unsigned long>(s.t_ms),
                       static_cast<double>(s.weight_g),
                       static_cast<double>(s.flow_gps));
}

inline bool parse_shot_sample_row(const char* line, ShotSample& out) {
  unsigned long t = 0;
  double w = 0, f = 0;
  if (std::sscanf(line, "%lu,%lf,%lf", &t, &w, &f) != 3) return false;
  out.t_ms = static_cast<uint32_t>(t);
  out.weight_g = static_cast<float>(w);
  out.flow_gps = static_cast<float>(f);
  return true;
}

}  // namespace core
