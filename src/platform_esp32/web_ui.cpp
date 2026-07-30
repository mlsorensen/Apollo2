#include "platform_esp32/web_ui.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <new>

#include "core/log_ring.h"
#include "core/shot_csv.h"
#include "core/system.h"
#include "platform_esp32/board_config.h"
#include "platform_esp32/webapp_dist.h"
#include "version.h"

namespace platform {

namespace {

const char* mode_name(core::ShotMode m) { return core::shot_mode_name(m); }

// Row batcher for the chunked JSON/CSV bodies.
//
// WebServer::sendContent costs THREE socket writes per call in chunked mode
// (chunk-size line, body, CRLF) plus a malloc/free for the size line — so a
// row-at-a-time loop turned a 600-sample shot into ~1800 tiny TCP segments.
// Each one is its own select()+send() and its own packet across the SDIO link
// to the C6: measured 7-8 s to serve a 7 KB CSV (~1 KB/s), and the buffer churn
// starved the BLE transport outright ("vhci_drv: Tx ... malloc failed").
//
// Accumulating ~1 KB per flush cuts that to a few dozen writes with identical
// bytes on the wire. Rows longer than the buffer pass straight through, so
// correctness never depends on the size chosen here.
class RowBatch {
 public:
  explicit RowBatch(WebServer& server) : server_(server) {}
  ~RowBatch() { flush(); }

  void add(const char* row) {
    const size_t n = std::strlen(row);
    if (n == 0) return;
    if (n >= sizeof(buf_)) {  // oversized row: flush order, then pass through
      flush();
      server_.sendContent(row, n);
      return;
    }
    if (used_ + n > sizeof(buf_)) flush();
    std::memcpy(buf_ + used_, row, n);
    used_ += n;
  }

  void flush() {
    if (used_ == 0) return;
    server_.sendContent(buf_, used_);
    used_ = 0;
  }

 private:
  WebServer& server_;
  char buf_[1024];
  size_t used_ = 0;
};

// "shot-20260615-0455" — sortable, locale-free, matches when the shot ran.
void shot_basename(const core::IShotStore& shots, uint32_t id, char* out,
                   size_t n) {
  int64_t unix_time = 0;
  core::ShotSummary page[16];
  for (int offset = 0;;) {
    const int got = shots.list(page, 16, offset);
    if (got == 0) break;
    offset += got;
    bool found = false;
    for (int i = 0; i < got; ++i)
      if (page[i].id == id) {
        unix_time = page[i].unix_time;
        found = true;
        break;
      }
    if (found) break;
  }
  if (unix_time == 0) {
    std::snprintf(out, n, "shot-%06lu", static_cast<unsigned long>(id));
    return;
  }
  const time_t t = static_cast<time_t>(unix_time);
  struct tm tm;
  localtime_r(&t, &tm);
  std::snprintf(out, n, "shot-%04d%02d%02d-%02d%02d", tm.tm_year + 1900,
                tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
}

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
  server_.on("/log", HTTP_GET, [this]() { handle_log(); });
  server_.on("/api/log", HTTP_GET, [this]() { handle_log(); });
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
#if defined(BOARD_DISPLAY_DSI)
  // P4: a dedicated task so serving never stalls LVGL (see the header for
  // why the S3 boards must NOT do this). Checked create — a silent failure
  // here means "web interface simply absent" (seen once on the 4.3C).
  if (xTaskCreatePinnedToCore(task_entry, "web_ui", 8192, this, 2, nullptr,
                              0) != pdPASS) {
    core::logf("WebUi: FAILED to create server task (internal RAM?) — "
               "web interface unavailable\n");
  }
#endif
}

bool WebUi::ensure_bound() {
  // lwip's tcpip thread only exists once something has initialized WiFi;
  // binding earlier asserts ("Invalid mbox").
  if (bound_) return true;
  if (WiFi.getMode() == WIFI_OFF) return false;
  server_.begin();
  bound_ = true;
  core::logf("WebUi: server up on :80\n");
  return true;
}

void WebUi::poll() {
#if !defined(BOARD_DISPLAY_DSI)
  // S3: all serving happens here, on the main loop — LVGL pauses during a
  // request, which keeps the RGB panel's PSRAM bandwidth intact (the panel
  // visibly jitters if we serve concurrently; see the header).
  if (ensure_bound()) server_.handleClient();
#endif
}

void WebUi::task_entry(void* self) { static_cast<WebUi*>(self)->run(); }

void WebUi::run() {
  while (!ensure_bound()) vTaskDelay(pdMS_TO_TICKS(250));
  for (;;) {
    server_.handleClient();
    // handleClient returns immediately when idle; a short sleep keeps this
    // task near-zero-cost between requests.
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

void WebUi::handle_root() {
  if (setup_ != nullptr && setup_->active()) {
    setup_->handle_root();  // the portal page (token or WiFi form)
    return;
  }
  // The app is ~100KB (gzipped at build time; the browser inflates it). Handing
  // all of it to send_P in one call bottoms out INTERNAL ram: that becomes a
  // single send() of 100KB, and since the source is memory-mapped flash the
  // stack has to stage the whole thing into DMA-capable internal buffers on its
  // way to the C6 over SDIO. Measured on the 5": internal free 121KB -> 15KB,
  // largest block 82KB -> 4KB, which is low enough that the DSI dirty sync
  // can't allocate its GDMA descriptor list and drops to a CPU copy.
  //
  // Slicing bounds what's in flight to kSlice, so payload size stops driving
  // peak ram. setContentLength with a REAL length keeps the WebServer's
  // _chunked flag false, so these appends carry no chunk framing — the bytes on
  // the wire are identical to the single-call version.
  constexpr size_t kSlice = 8 * 1024;
  server_.sendHeader("Content-Encoding", "gzip");
  server_.setContentLength(kWebAppGzLen);
  server_.send(200, "text/html", "");
  for (size_t off = 0; off < kWebAppGzLen; off += kSlice) {
    const size_t n = kWebAppGzLen - off < kSlice ? kWebAppGzLen - off : kSlice;
    server_.sendContent_P(reinterpret_cast<const char*>(kWebAppGz + off), n);
  }
}

void WebUi::handle_log() {
  // Snapshot the whole ring (mutex inside), then send in slices — same
  // peak-RAM reasoning as handle_root. The copy goes to PSRAM where the ring
  // itself lives; boards without PSRAM fall back to the (small) internal ring.
  const size_t cap = core::log_ring().capacity() + 1;
  char* buf = static_cast<char*>(
      heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (buf == nullptr) buf = static_cast<char*>(malloc(cap));
  if (buf == nullptr) {
    server_.send(503, "text/plain", "log unavailable (no memory)");
    return;
  }
  const size_t n = core::log_ring().snapshot_tail(buf, cap);
  constexpr size_t kSlice = 8 * 1024;
  server_.setContentLength(n);
  server_.send(200, "text/plain; charset=utf-8", "");
  for (size_t off = 0; off < n; off += kSlice) {
    const size_t chunk = n - off < kSlice ? n - off : kSlice;
    server_.sendContent(buf + off, chunk);
  }
  free(buf);
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
  RowBatch batch(server_);
  batch.add("{\"shots\":[");
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
      batch.add(row);
      first = false;
    }
  }
  batch.add("]}");
  batch.flush();
  server_.sendContent("");  // end of chunked body
}

void WebUi::handle_shot_csv() {
  const uint32_t id = static_cast<uint32_t>(server_.arg("id").toInt());
  // Regenerated from the record via the shared format helpers — no filesystem
  // coupling, and it works for any store implementation.
  //
  // PSRAM: a ShotRecord is ~7KB and plain `new` would take that from the
  // internal heap, which is the scarce pool here (ui/app.cpp keeps its two
  // staging records in the LVGL pool for the same reason). Falls back to the
  // default heap on boards without PSRAM.
  void* mem = heap_caps_malloc(sizeof(core::ShotRecord),
                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (mem == nullptr) mem = malloc(sizeof(core::ShotRecord));
  auto* rec = mem != nullptr ? new (mem) core::ShotRecord : nullptr;
  if (rec == nullptr || !shots_->read(id, *rec)) {
    if (rec != nullptr) rec->~ShotRecord();
    free(mem);
    server_.send(404, "text/plain", "no such shot");
    return;
  }
  char base[40], disp[80];
  shot_basename(*shots_, id, base, sizeof(base));
  std::snprintf(disp, sizeof(disp), "attachment; filename=%s.csv", base);
  server_.sendHeader("Content-Disposition", disp);
  server_.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server_.send(200, "text/csv", "");
  {
    RowBatch batch(server_);
    batch.add(core::kShotSamplesHeader);
    char row[48];
    for (int i = 0; i < rec->n_samples; ++i) {
      core::format_shot_sample_row(row, sizeof(row), rec->samples[i]);
      batch.add(row);
    }
  }  // batch flushes here, before the terminating empty chunk
  server_.sendContent("");
  rec->~ShotRecord();
  free(rec);
}


}  // namespace platform
