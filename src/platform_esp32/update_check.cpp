#include "platform_esp32/update_check.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>

#include <cstdio>
#include <cstring>

#include "core/log_ring.h"
#include "core/network.h"
#include "core/system.h"
#include "platform_esp32/config.h"
#include "platform_esp32/network.h"
#include "version.h"

namespace platform {
namespace {

constexpr char kSiteBase[] = "https://mlsorensen.github.io/Apollo2";
constexpr unsigned long kCheckIntervalMs = 24ul * 60ul * 60ul * 1000ul;  // daily
constexpr unsigned long kRetryMs = 60ul * 60ul * 1000ul;  // failed fetch: hourly
constexpr size_t kMaxNotesBytes = 4096;  // bound RAM; notes truncate past this

// Parse "v1.2.3" or "1.2.3" into a comparable triple. Returns false on
// anything that isn't strict major.minor.patch — callers treat that as
// "not newer" (fail quiet, try again next round).
bool parse_semver(const char* s, int out[3]) {
  if (*s == 'v') ++s;
  return std::sscanf(s, "%d.%d.%d", &out[0], &out[1], &out[2]) == 3;
}

// True iff a is numerically newer than b (major, then minor, then patch).
// NEVER compare version strings: "0.9.0" > "0.10.0" lexicographically.
bool semver_newer(const char* a, const char* b) {
  int va[3], vb[3];
  if (!parse_semver(a, va) || !parse_semver(b, vb)) return false;
  for (int i = 0; i < 3; ++i) {
    if (va[i] != vb[i]) return va[i] > vb[i];
  }
  return false;
}

// Pull the first "vX.Y.Z" out of releases.json (a JSON array, newest first).
// A full JSON parse is overkill for ["v0.10.0", ...] — scan for the first
// quoted token that parses as a version.
std::string first_version(const String& body) {
  int start = -1;
  for (int i = 0; i < static_cast<int>(body.length()); ++i) {
    if (body[i] != '"') continue;
    if (start < 0) {
      start = i + 1;
    } else {
      std::string tok(body.c_str() + start, i - start);
      int v[3];
      if (parse_semver(tok.c_str(), v)) return tok;
      start = -1;
    }
  }
  return {};
}

}  // namespace

UpdateCheck::UpdateCheck(Config& config, Network& network)
    : config_(config), network_(network) {}

void UpdateCheck::poll() {
  if (in_flight_.load()) return;
  if (!config_.update_check_enabled()) return;
  // Gate: station up AND NTP has actually synced this session — the working
  // proof that the internet (not just the LAN) is reachable.
  if (network_.status() != core::NetState::Connected || !network_.ntp_synced()) return;
  if (next_check_ms_ != 0 && millis() < next_check_ms_) return;

  in_flight_.store(true);
  // Short-lived task so the TLS handshake (~seconds, ~45KB transient heap)
  // never runs on the LVGL loop. Stack is generous for mbedTLS.
  if (xTaskCreatePinnedToCore(task_entry, "updchk", 12 * 1024, this, 1, nullptr,
                              0) != pdPASS) {
    in_flight_.store(false);
    next_check_ms_ = millis() + kRetryMs;  // heap-tight moment; try later
  }
}

void UpdateCheck::task_entry(void* arg) {
  auto* self = static_cast<UpdateCheck*>(arg);
  self->run_check();
  self->in_flight_.store(false);
  vTaskDelete(nullptr);
}

void UpdateCheck::run_check() {
  next_check_ms_ = millis() + kRetryMs;  // assume failure; success extends below

  NetworkClientSecure client;
  // Notify-only endpoint: the response can only ever produce an on-screen
  // notice, and installing happens through the browser (with real TLS). A
  // tampered response can't deliver code, so skipping cert validation trades
  // a spoofable notification for not carrying a CA bundle + its rotations.
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(10000);

  String releases_url = String(kSiteBase) + "/releases.json";
  if (!http.begin(client, releases_url)) return;
  if (http.GET() != HTTP_CODE_OK) {
    http.end();
    core::logf("UpdateCheck: releases.json fetch failed\n");
    return;
  }
  const String body = http.getString();
  http.end();

  const std::string latest = first_version(body);
  if (latest.empty()) {
    core::logf("UpdateCheck: no version in releases.json\n");
    return;
  }

  std::string notes;
  if (semver_newer(latest.c_str(), fw::kVersion)) {
    // Release notes are best-effort (older releases predate notes.txt).
    String notes_url = String(kSiteBase) + "/" + latest.c_str() + "/notes.txt";
    if (http.begin(client, notes_url)) {
      if (http.GET() == HTTP_CODE_OK) {
        String n = http.getString();
        notes.assign(n.c_str(), std::min(static_cast<size_t>(n.length()),
                                         kMaxNotesBytes));
      }
      http.end();
    }
    core::logf("UpdateCheck: v%s running, %s available\n", fw::kVersion,
               latest.c_str());
  }

  {
    std::lock_guard<std::mutex> lock(mu_);
    latest_ = latest;
    notes_ = notes;
  }
  next_check_ms_ = millis() + kCheckIntervalMs;  // success: next check tomorrow
}

core::UpdateInfo UpdateCheck::info() const {
  core::UpdateInfo out;
  std::lock_guard<std::mutex> lock(mu_);
  if (latest_.empty()) return out;
  out.version = latest_;
  out.notes = notes_;
  out.available = semver_newer(latest_.c_str(), fw::kVersion) &&
                  latest_ != config_.skipped_update();
  return out;
}

void UpdateCheck::skip_current() {
  std::string v;
  {
    std::lock_guard<std::mutex> lock(mu_);
    v = latest_;
  }
  if (!v.empty()) config_.set_skipped_update(v);
}

bool UpdateCheck::enabled() const { return config_.update_check_enabled(); }

void UpdateCheck::set_enabled(bool on) { config_.set_update_check_enabled(on); }

}  // namespace platform
