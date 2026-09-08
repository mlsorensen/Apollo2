#include "platform_esp32/update_check.h"

#include <Arduino.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <hal/efuse_hal.h>
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

// Per-silicon image variant. "" today; when the P4 boards start shipping in
// both revision generations and the workflow publishes dual images, rev3
// units select the "-rev3" file here (efuse_hal_chip_revision() >= 300).
const char* update_variant_suffix() { return ""; }

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
  use_psram_for_tls();
  std::string body;
  if (!https_get(std::string(kSiteBase) + "/releases.json", body, 2048)) {
    core::logf("UpdateCheck: releases.json fetch failed\n");
    return;
  }
  const std::string latest = first_version(body);
  if (latest.empty()) {
    core::logf("UpdateCheck: no version in releases.json\n");
    return;
  }

  std::string notes;
  if (semver_newer(latest.c_str(), fw::kVersion)) {
    // Release notes are best-effort (older releases predate notes.txt).
    https_get(std::string(kSiteBase) + "/" + latest + "/notes.txt", notes,
              kMaxNotesBytes);
    core::logf("UpdateCheck: v%s running, %s available\n", fw::kVersion,
               latest.c_str());
  }

  {
    std::lock_guard<std::mutex> lock(mu_);
    latest_ = latest;
    notes_ = notes;
  }
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

bool UpdateCheck::check_at_startup() const {
  return config_.update_check_mode() != 0;  // 0 off, else on
}

void UpdateCheck::set_check_at_startup(bool on) {
  config_.set_update_check_mode(on ? 1 : 0);
}

void UpdateCheck::start_install() {
  if (in_flight_.load()) return;
  std::string v;
  {
    std::lock_guard<std::mutex> lock(mu_);
    v = latest_;
  }
  if (v.empty() || !semver_newer(v.c_str(), fw::kVersion)) return;
  cancel_.store(false);
  install_pct_.store(0);
  install_state_.store(static_cast<int>(core::InstallState::kDownloading));
  in_flight_.store(true);
  if (xTaskCreatePinnedToCore(install_task_entry, "updinst", 12 * 1024, this, 1,
                              nullptr, 0) != pdPASS) {
    in_flight_.store(false);
    std::lock_guard<std::mutex> lock(mu_);
    install_error_ = "out of memory";
    install_state_.store(static_cast<int>(core::InstallState::kError));
  }
}

core::InstallStatus UpdateCheck::install_status() const {
  core::InstallStatus s;
  s.state = static_cast<core::InstallState>(install_state_.load());
  s.percent = install_pct_.load();
  if (s.state == core::InstallState::kError) {
    std::lock_guard<std::mutex> lock(mu_);
    s.error = install_error_;
  }
  return s;
}

void UpdateCheck::install_task_entry(void* arg) {
  auto* self = static_cast<UpdateCheck*>(arg);
  self->run_install();
  self->in_flight_.store(false);
  vTaskDelete(nullptr);
}

void UpdateCheck::run_install() {
  const auto fail = [this](const char* why) {
    core::logf("UpdateInstall: FAILED - %s\n", why);
    {
      std::lock_guard<std::mutex> lock(mu_);
      install_error_ = why;
    }
    install_state_.store(static_cast<int>(core::InstallState::kError));
  };

  std::string version;
  {
    std::lock_guard<std::mutex> lock(mu_);
    version = latest_;
  }
  use_psram_for_tls();
  const std::string url = std::string(kSiteBase) + "/" + version +
                          "/firmware/app/" + board::kUpdateSlug +
                          update_variant_suffix() + ".bin";
  core::logf("UpdateInstall: %s\n", url.c_str());

  // RANGED download: the esp-hosted SDIO link can't sustain a full-speed
  // multi-MB stream (its RX buffers fill faster than we drain and it asserts),
  // so pull the image in bounded pieces via HTTP Range requests. Each piece is
  // a short transfer with an idle gap after, and is flashed before the next —
  // no sustained high throughput, and only a small PSRAM buffer needed.
  constexpr int kChunk = 128 * 1024;
  uint8_t* buf = static_cast<uint8_t*>(heap_caps_malloc(kChunk, MALLOC_CAP_SPIRAM));
  if (buf == nullptr) { fail("out of memory"); return; }

  const auto range_get = [&](int off, int want, int& out_len, int& out_total) -> bool {
    char range[48];
    std::snprintf(range, sizeof(range), "bytes=%d-%d", off, off + want - 1);
    esp_http_client_config_t http = {};
    http.url = url.c_str();
    http.crt_bundle_attach = esp_crt_bundle_attach;
    http.timeout_ms = 15000;
    esp_http_client_handle_t c = esp_http_client_init(&http);
    if (c == nullptr) return false;
    esp_http_client_set_header(c, "Range", range);
    bool ok = false;
    out_len = 0;
    out_total = 0;
    if (esp_http_client_open(c, 0) == ESP_OK) {
      const int clen = esp_http_client_fetch_headers(c);
      const int status = esp_http_client_get_status_code(c);
      // 206 Partial Content: "Content-Range: bytes X-Y/TOTAL" gives the size.
      char cr[64] = {};
      char* crp = cr;
      if (esp_http_client_get_header(c, "Content-Range", &crp) == ESP_OK && crp) {
        const char* slash = std::strrchr(crp, '/');
        if (slash) out_total = atoi(slash + 1);
      }
      if (status == 206 || status == 200) {
        int n = 0;
        while (n < clen && n < want) {
          const int r = esp_http_client_read(c, reinterpret_cast<char*>(buf + n),
                                             std::min(4096, want - n));
          if (r <= 0) break;
          n += r;
        }
        out_len = n;
        ok = n > 0;
      } else {
        core::logf("UpdateInstall: HTTP %d for range %s\n", status, range);
      }
      esp_http_client_close(c);
    }
    esp_http_client_cleanup(c);
    return ok;
  };

  // First range establishes the total size and opens the OTA session.
  int total = 0, first_len = 0;
  if (!range_get(0, kChunk, first_len, total) || total <= 0) {
    heap_caps_free(buf);
    fail("download failed (wrong board image?)");
    return;
  }
  const esp_partition_t* part = esp_ota_get_next_update_partition(nullptr);
  esp_ota_handle_t ota = 0;
  if (esp_ota_begin(part, total, &ota) != ESP_OK) {
    heap_caps_free(buf);
    fail("OTA begin failed");
    return;
  }

  int off = 0, len = first_len;
  bool ok = true;
  while (off < total) {
    if (off > 0) {  // range 0 already fetched
      int rtotal = 0;
      if (!range_get(off, std::min(kChunk, total - off), len, rtotal)) {
        ok = false;
        break;
      }
    }
    if (esp_ota_write(ota, buf, len) != ESP_OK) { ok = false; break; }
    off += len;
    install_pct_.store(off * 100 / total);
    if (cancel_.load()) { ok = false; break; }
    delay(20);  // let the SDIO RX drain fully before the next burst
  }
  heap_caps_free(buf);

  if (cancel_.load()) {
    esp_ota_abort(ota);
    install_state_.store(static_cast<int>(core::InstallState::kIdle));
    return;
  }
  if (!ok || off != total) { esp_ota_abort(ota); fail("download interrupted"); return; }

  install_state_.store(static_cast<int>(core::InstallState::kVerifying));
  esp_err_t e = esp_ota_end(ota);  // validates the image
  if (e == ESP_OK) e = esp_ota_set_boot_partition(part);
  if (e != ESP_OK) {
    core::logf("UpdateInstall: finalize failed (%s)\n", esp_err_to_name(e));
    fail("image rejected");
    return;
  }
  core::logf("UpdateInstall: %s written and set to boot\n", version.c_str());
  install_state_.store(static_cast<int>(core::InstallState::kReady));
}

}  // namespace platform
