#include "platform_esp32/update_check.h"

#include <Arduino.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_https_ota.h>
#include <hal/efuse_hal.h>

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
constexpr unsigned long kCheckIntervalMs = 24ul * 60ul * 60ul * 1000ul;  // daily
constexpr unsigned long kRetryMs = 60ul * 60ul * 1000ul;  // failed fetch: hourly
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

// The P4's hosted radio asserts (sdio_rx_get_buffer) when the DMA-capable
// internal pool runs dry, and the prebuilt mbedtls forces ALL TLS memory
// internal (MBEDTLS_INTERNAL_MEM_ALLOC) — a fetch is a ~55KB internal spike.
// Log the pool around TLS work; the gate below refuses to start a fetch
// without comfortable headroom. (Plain internal-free is misleading on the
// P4 — the DMA subset is what the radio starves on.)
void log_heap(const char* tag) {
  core::logf("UpdateCheck: heap[%s] dma free=%u largest=%u int free=%u\n", tag,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
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
    log_heap("handshake");  // peak: TLS session is up right here
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

void UpdateCheck::poll() {
  if (in_flight_.load()) return;
  if (!config_.update_check_enabled()) return;
  // Gate: station up AND NTP has actually synced this session — the working
  // proof that the internet (not just the LAN) is reachable.
  if (network_.status() != core::NetState::Connected || !network_.ntp_synced()) return;
  if (next_check_ms_ != 0 && millis() < next_check_ms_) return;

  // HARD GATE, measured on the P4-5 (2026-09-07): one TLS connection eats
  // ~34KB of the DMA-capable pool (mbedtls record buffers, force-internal in
  // the prebuilt core), and the hosted radio asserts (sdio_rx_get_buffer ->
  // panic reboot) when that pool bottoms out. Only fetch with enough room
  // for the spike PLUS a radio floor. On today's P4 steady-state (~40KB free,
  // ~35KB largest) this never passes — the check is effectively disabled
  // there until the boot-time-check design lands; the S3's on-chip radio
  // pre-reserves its buffers, so it clears the gate or fails gracefully.
  constexpr size_t kTlsSpike = 36 * 1024, kRadioFloor = 24 * 1024;
  if (heap_caps_get_largest_free_block(MALLOC_CAP_DMA) < kTlsSpike ||
      heap_caps_get_free_size(MALLOC_CAP_DMA) < kTlsSpike + kRadioFloor) {
    if (next_check_ms_ == 0) log_heap("deferred-low-dma");  // log once, not 4 Hz
    next_check_ms_ = millis() + kRetryMs;  // re-evaluate hourly
    return;
  }

  in_flight_.store(true);
  // Short-lived task so the TLS handshake (~seconds, ~45KB transient heap)
  // never runs on the LVGL loop. Stack is generous for mbedTLS.
  if (xTaskCreatePinnedToCore(check_task_entry, "updchk", 12 * 1024, this, 1,
                              nullptr, 0) != pdPASS) {
    in_flight_.store(false);
    next_check_ms_ = millis() + kRetryMs;  // heap-tight moment; try later
  }
}

void UpdateCheck::check_task_entry(void* arg) {
  auto* self = static_cast<UpdateCheck*>(arg);
  self->run_check();
  self->in_flight_.store(false);
  vTaskDelete(nullptr);
}

void UpdateCheck::run_check() {
  next_check_ms_ = millis() + kRetryMs;  // assume failure; success extends below

  log_heap("pre-tls");
  std::string body;
  const bool got = https_get(std::string(kSiteBase) + "/releases.json", body, 2048);
  log_heap("post-tls");
  if (!got) {
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

void UpdateCheck::start_install() {
  if (in_flight_.load()) return;  // a check or install is already running
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

void UpdateCheck::cancel_install() { cancel_.store(true); }

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
  const std::string url = std::string(kSiteBase) + "/" + version +
                          "/firmware/app/" + board::kUpdateSlug +
                          update_variant_suffix() + ".bin";
  core::logf("UpdateInstall: %s -> inactive OTA slot\n", url.c_str());

  // esp_https_ota streams straight into the inactive app slot, validates the
  // image header, and (on finish) flips the boot partition. With
  // BOOTLOADER_APP_ROLLBACK_ENABLE the new image boots PENDING_VERIFY and the
  // bootloader reverts it unless main.cpp's health check marks it valid.
  esp_http_client_config_t http = {};
  http.url = url.c_str();
  http.crt_bundle_attach = esp_crt_bundle_attach;
  http.timeout_ms = 15000;
  http.keep_alive_enable = true;
  esp_https_ota_config_t ota = {};
  ota.http_config = &http;

  esp_https_ota_handle_t handle = nullptr;
  if (esp_https_ota_begin(&ota, &handle) != ESP_OK) {
    fail("download failed (wrong board image missing?)");
    return;
  }
  const int total = esp_https_ota_get_image_size(handle);

  esp_err_t err;
  while ((err = esp_https_ota_perform(handle)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
    if (total > 0) {
      install_pct_.store(esp_https_ota_get_image_len_read(handle) * 100 / total);
    }
    if (cancel_.load()) {
      esp_https_ota_abort(handle);
      core::logf("UpdateInstall: canceled\n");
      install_state_.store(static_cast<int>(core::InstallState::kIdle));
      return;
    }
  }
  if (err != ESP_OK) {
    esp_https_ota_abort(handle);
    fail("download interrupted");
    return;
  }
  if (!esp_https_ota_is_complete_data_received(handle)) {
    esp_https_ota_abort(handle);
    fail("download truncated");
    return;
  }

  install_state_.store(static_cast<int>(core::InstallState::kVerifying));
  if (esp_https_ota_finish(handle) != ESP_OK) {  // validates + sets boot slot
    fail("image rejected");
    return;
  }
  core::logf("UpdateInstall: %s written and set to boot\n", version.c_str());
  install_state_.store(static_cast<int>(core::InstallState::kReady));
}

}  // namespace platform
