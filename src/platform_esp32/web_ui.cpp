#include "platform_esp32/web_ui.h"

#include <Arduino.h>
#include <WiFi.h>

#include <cstdio>

#include "core/shot_csv.h"
#include "platform_esp32/webapp_dist.h"
#include "version.h"

namespace platform {

namespace {

const char* mode_name(core::ShotMode m) { return core::shot_mode_name(m); }

}  // namespace

void WebUi::begin(TokenSetup& setup, core::IShotStore& shots, core::IClock& clock,
                  const char* name, std::function<WebTheme()> theme) {
  setup_ = &setup;
  shots_ = &shots;
  clock_ = &clock;
  name_ = name;
  theme_ = std::move(theme);
  setup.attach(server_);

  server_.on("/", HTTP_GET, [this]() { handle_root(); });
  server_.on("/save", HTTP_POST, [this]() { setup_->handle_save(); });
  server_.on("/wifi", HTTP_POST, [this]() { setup_->handle_wifi(); });
  server_.on("/api/summary", HTTP_GET, [this]() { handle_summary(); });
  server_.on("/api/shots", HTTP_GET, [this]() { handle_shots(); });
  server_.on("/api/shot.csv", HTTP_GET, [this]() { handle_shot_csv(); });
  server_.on("/api/shot.png", HTTP_GET, [this]() { handle_shot_png(); });
  server_.onNotFound([this]() {
    // During a portal session, funnel everything to the setup page (phones
    // probe random URLs); otherwise a plain 404.
    if (setup_ != nullptr && setup_->active()) {
      server_.sendHeader("Location", "/");
      server_.send(302, "text/plain", "");
    } else {
      server_.send(404, "text/plain", "not found");
    }
  });
  // Socket binding is deferred to poll(): lwip's tcpip thread only exists
  // once something has initialized WiFi, and binding earlier asserts.
}

void WebUi::poll() {
  if (!started_) {
    if (WiFi.getMode() == WIFI_OFF) return;  // no network stack yet
    server_.begin();
    started_ = true;
    Serial.println("WebUi: server up on :80");
  }
  server_.handleClient();
}

void WebUi::handle_root() {
  if (setup_ != nullptr && setup_->active()) {
    setup_->handle_root();  // the portal page (token or WiFi form)
    return;
  }
  server_.sendHeader("Content-Encoding", "gzip");
  server_.send_P(200, "text/html", reinterpret_cast<const char*>(kWebAppGz),
                 kWebAppGzLen);
}

void WebUi::handle_summary() {
  const WebTheme t = theme_ ? theme_() : WebTheme{};
  const core::StorageInfo si = shots_->storage();
  const core::ShotStats st = shots_->stats(clock_->now_unix());
  char buf[640];
  std::snprintf(
      buf, sizeof(buf),
      "{\"name\":\"%s\",\"version\":\"%s\","
      "\"theme\":{\"bg\":%lu,\"rail\":%lu,\"card\":%lu,\"text\":%lu,"
      "\"muted\":%lu,\"accent\":%lu,\"ok\":%lu,\"warn\":%lu,\"alert\":%lu},"
      "\"storage\":{\"total\":%llu,\"free\":%llu,\"full\":%s},"
      "\"stats\":{\"total\":%d,\"life\":%.1f,\"d30\":%.1f,\"since\":%lld}}",
      name_, fw::kVersion, static_cast<unsigned long>(t.bg),
      static_cast<unsigned long>(t.rail), static_cast<unsigned long>(t.card),
      static_cast<unsigned long>(t.text), static_cast<unsigned long>(t.muted),
      static_cast<unsigned long>(t.accent), static_cast<unsigned long>(t.ok),
      static_cast<unsigned long>(t.warn), static_cast<unsigned long>(t.alert),
      static_cast<unsigned long long>(si.total_bytes),
      static_cast<unsigned long long>(si.free_bytes),
      si.full ? "true" : "false", st.total,
      static_cast<double>(st.acc_lifetime_pct),
      static_cast<double>(st.acc_30d_pct),
      static_cast<long long>(shots_->stats_since()));
  server_.send(200, "application/json", buf);
}

void WebUi::handle_shots() {
  // Chunked: constant memory no matter how long the history is.
  server_.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server_.send(200, "application/json", "");
  server_.sendContent("{\"shots\":[");
  constexpr int kPage = 16;
  core::ShotSummary page[kPage];
  char row[192];
  int offset = 0;
  bool first = true;
  for (;;) {
    const int n = shots_->list(page, kPage, offset);
    if (n == 0) break;
    offset += n;
    for (int i = 0; i < n; ++i) {
      const core::ShotSummary& s = page[i];
      std::snprintf(row, sizeof(row),
                    "%s{\"id\":%lu,\"unix\":%lld,\"duration_ms\":%lu,"
                    "\"target_g\":%.1f,\"final_g\":%.1f,\"avg_gps\":%.2f,"
                    "\"mode\":\"%s\",\"wired\":%s}",
                    first ? "" : ",", static_cast<unsigned long>(s.id),
                    static_cast<long long>(s.unix_time),
                    static_cast<unsigned long>(s.duration_ms),
                    static_cast<double>(s.target_g),
                    static_cast<double>(s.final_g),
                    static_cast<double>(s.avg_gps), mode_name(s.mode),
                    s.wired ? "true" : "false");
      server_.sendContent(row);
      first = false;
    }
  }
  server_.sendContent("]}");
  server_.sendContent("");  // end of chunked body
}

void WebUi::handle_shot_csv() {
  const uint32_t id = static_cast<uint32_t>(server_.arg("id").toInt());
  // Regenerated from the record via the shared format helpers — no filesystem
  // coupling, and it works for any store implementation.
  auto* rec = new (std::nothrow) core::ShotRecord;
  if (rec == nullptr || !shots_->read(id, *rec)) {
    delete rec;
    server_.send(404, "text/plain", "no such shot");
    return;
  }
  char disp[64];
  std::snprintf(disp, sizeof(disp), "attachment; filename=shot-%06lu.csv",
                static_cast<unsigned long>(id));
  server_.sendHeader("Content-Disposition", disp);
  server_.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server_.send(200, "text/csv", "");
  server_.sendContent(core::kShotSamplesHeader);
  char row[48];
  for (int i = 0; i < rec->n_samples; ++i) {
    core::format_shot_sample_row(row, sizeof(row), rec->samples[i]);
    server_.sendContent(row);
  }
  server_.sendContent("");
  delete rec;
}

void WebUi::handle_shot_png() {
  const uint32_t id = static_cast<uint32_t>(server_.arg("id").toInt());
  char path[96];
  if (!shots_->image_path(id, path, sizeof(path))) {
    server_.send(404, "text/plain", "no image for this shot");
    return;
  }
  FILE* f = std::fopen(path, "rb");
  if (f == nullptr) {
    server_.send(404, "text/plain", "no image for this shot");
    return;
  }
  char disp[64];
  std::snprintf(disp, sizeof(disp), "attachment; filename=shot-%06lu.png",
                static_cast<unsigned long>(id));
  server_.sendHeader("Content-Disposition", disp);
  server_.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server_.send(200, "image/png", "");
  static char chunk[1024];
  size_t n;
  while ((n = std::fread(chunk, 1, sizeof(chunk), f)) > 0)
    server_.sendContent(chunk, n);
  std::fclose(f);
  server_.sendContent("");
}

}  // namespace platform
