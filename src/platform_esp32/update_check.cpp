#include "platform_esp32/update_check.h"

#include <Arduino.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <mbedtls/platform.h>

#include <cstdio>
#include <cstring>

#include "core/network.h"
#include "core/system.h"
#include "platform_esp32/board_config.h"
#include "platform_esp32/config.h"
#include "platform_esp32/network.h"
#include "version.h"

namespace platform {
namespace {

constexpr char kSiteBase[] = "https://mlsorensen.github.io/Apollo2";
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
std::string first_version(const std::string& body) {
  int start = -1;
  for (int i = 0; i < static_cast<int>(body.size()); ++i) {
    if (body[i] != '"') continue;
    if (start < 0) {
      start = i + 1;
    } else {
      std::string tok(body.data() + start, i - start);
      int v[3];
      if (parse_semver(tok.c_str(), v)) return tok;
      start = -1;
    }
  }
  return {};
}

// ROOT-CAUSE FIX (measured 2026-09-07): the prebuilt core forces mbedTLS to
// allocate from internal RAM (MBEDTLS_INTERNAL_MEM_ALLOC), so TLS's ~46KB of
// record buffers land in the scarce internal-DMA pool the hosted C6 radio's
// SDIO driver also uses — and starve it (sdio_rx_get_buffer assert). Pointing
// mbedTLS at PSRAM at runtime moves those buffers out, so TLS and BLE coexist
// (handshake DMA-free 29K vs the 6K that used to panic). rc=0 on this core.
void* psram_calloc(size_t n, size_t sz) {
  return heap_caps_calloc(n, sz, MALLOC_CAP_SPIRAM);
}
// Point mbedTLS at PSRAM so TLS's ~46KB of record buffers don't land in the
// scarce internal-DMA pool the hosted C6 radio's SDIO driver shares and starve
// it — lets the check's TLS fetch run with BLE up. Idempotent.
void use_psram_for_tls() {
  static bool done = false;
  if (done) return;
  done = true;
  if (mbedtls_platform_set_calloc_free(psram_calloc, free) != 0)
    core::logf("UpdateCheck: WARNING mbedTLS PSRAM allocator refused\n");
}

// Small authenticated GET into a bounded string. Trust = the cert bundle
// embedded in the core libs (only reachable through the IDF client config's
// crt_bundle_attach — the Arduino wrapper can't use the built-in bundle).
bool https_get(const std::string& url, std::string& out, size_t cap) {
  esp_http_client_config_t cfg = {};
  cfg.url = url.c_str();
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.timeout_ms = 10000;
  esp_http_client_handle_t h = esp_http_client_init(&cfg);
  if (h == nullptr) return false;
  bool ok = false;
  if (esp_http_client_open(h, 0) == ESP_OK) {
    esp_http_client_fetch_headers(h);
    if (esp_http_client_get_status_code(h) == 200) {
      char buf[512];
      int n;
      while ((n = esp_http_client_read(h, buf, sizeof(buf))) > 0) {
        const size_t room = cap - out.size();
        out.append(buf, std::min(static_cast<size_t>(n), room));
        if (out.size() >= cap) break;
      }
      ok = true;
    }
    esp_http_client_close(h);
  }
  esp_http_client_cleanup(h);
  return ok;
}

// Retry wrapper: a `sock < 0` (Connection failed) from https_get is transient —
// the previous fetch's TCP socket can still be in TIME_WAIT and the small LWIP
// socket pool (shared with BLE, the web server and NTP) briefly has none free.
// A short backoff lets it clear. Fixes intermittent empty release notes and
// "couldn't contact the update server" on the native-WiFi S3 boards.
bool https_get_retry(const std::string& url, std::string& out, size_t cap,
                     int tries) {
  for (int attempt = 1; attempt <= tries; ++attempt) {
    out.clear();
    if (https_get(url, out, cap)) return true;
    if (attempt < tries) {
      core::logf("UpdateCheck: fetch attempt %d/%d failed, retrying\n", attempt,
                 tries);
      delay(700);
    }
  }
  return false;
}

}  // namespace

UpdateCheck::UpdateCheck(Config& config, Network& network)
    : config_(config), network_(network) {}

bool UpdateCheck::network_ready() const {
  return network_.status() == core::NetState::Connected && network_.ntp_synced();
}

bool UpdateCheck::take_check_request() { return check_req_.exchange(false); }

void UpdateCheck::begin_check() {
  if (in_flight_.load()) return;
  if (!network_ready()) {
    core::logf("UpdateCheck: no WiFi/NTP; check skipped\n");
    return;  // no network -> the whole feature is inert
  }
  in_flight_.store(true);
  if (xTaskCreatePinnedToCore(check_task_entry, "updchk", 12 * 1024, this, 1,
                              nullptr, 0) != pdPASS) {
    in_flight_.store(false);
  }
}

void UpdateCheck::check_task_entry(void* arg) {
  auto* self = static_cast<UpdateCheck*>(arg);
  self->run_check();
  self->in_flight_.store(false);
  self->seq_.fetch_add(1);  // wake the UI: a check finished
  vTaskDelete(nullptr);
}

void UpdateCheck::run_check() {
  last_ok_.store(false);  // set true only once we've actually read a version
  use_psram_for_tls();
  std::string body;
  if (!https_get_retry(std::string(kSiteBase) + "/releases.json", body, 2048,
                       3)) {
    core::logf("UpdateCheck: releases.json fetch failed\n");
    return;
  }
  const std::string latest = first_version(body);
  if (latest.empty()) {
    core::logf("UpdateCheck: no version in releases.json\n");
    return;
  }

  std::string notes;
  bool got_notes = false;
  if (semver_newer(latest.c_str(), fw::kVersion)) {
    // Release notes are best-effort (older releases predate notes.txt), but
    // retry so a transient socket failure doesn't leave the notice blank.
    got_notes = https_get_retry(
        std::string(kSiteBase) + "/" + latest + "/notes.txt", notes,
        kMaxNotesBytes, 3);
    core::logf("UpdateCheck: v%s running, %s available\n", fw::kVersion,
               latest.c_str());
  }

  {
    std::lock_guard<std::mutex> lock(mu_);
    const bool same_version = (latest_ == latest);
    latest_ = latest;
    // Don't let a transient notes-fetch failure blank out notes we already have
    // for this same version; a newer version always replaces them.
    if (got_notes || !same_version) notes_ = notes;
  }
  last_ok_.store(true);  // reached the server and read a version
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

int UpdateCheck::check_cadence() const { return config_.update_check_mode(); }

void UpdateCheck::set_check_cadence(int mode) {
  config_.set_update_check_mode(mode);
}

}  // namespace platform
