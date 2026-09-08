#include "platform_esp32/install_mode.h"

#include <Arduino.h>

#include "core/system.h"
#include "platform_esp32/board_config.h"
#include "platform_esp32/config.h"

#if defined(BOARD_DISPLAY_DSI) || defined(BOARD_DISPLAY_RGB)
#include <WiFi.h>
#include <time.h>

#include <lvgl.h>

#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

#include "platform_esp32/display.h"

namespace platform {
namespace install_mode {
namespace {

constexpr char kSiteBase[] = "https://mlsorensen.github.io/Apollo2";

lv_obj_t* g_title = nullptr;
lv_obj_t* g_bar = nullptr;
lv_obj_t* g_pct = nullptr;
lv_obj_t* g_status = nullptr;

// ---- board-specific display backend -------------------------------------
// DSI (hosted-radio P4): a bulk download can't coexist with the full display
// (it fragments the internal-DMA pool the SDIO RX needs), so bring up a LIGHT
// panel (num_fbs=2, no use_dma2d/async) and drive LVGL DIRECT into a landscape
// buffer that we rotate 90° CW and present. RGB (native-WiFi S3): no DMA-pool
// problem, so just use the normal full display; the only reason to blank is the
// flash-write cache-disable strobe.
#if defined(BOARD_DISPLAY_DSI)
constexpr int kUiW = board::kLcdNativeH;  // landscape
constexpr int kUiH = board::kLcdNativeW;
uint16_t* g_port = nullptr;
uint16_t* g_land = nullptr;

void flush_cb(lv_display_t* d, const lv_area_t* /*a*/, uint8_t* /*px*/) {
  if (!lv_display_flush_is_last(d)) { lv_display_flush_ready(d); return; }
  for (int ly = 0; ly < kUiH; ++ly) {
    const int px = board::kLcdNativeW - 1 - ly;
    const uint16_t* srow = g_land + (size_t)ly * kUiW;
    for (int lx = 0; lx < kUiW; ++lx)
      g_port[(size_t)lx * board::kLcdNativeW + px] = srow[lx];
  }
  install_panel_present(g_port);
  lv_display_flush_ready(d);
}

bool disp_begin() {
  if (!install_panel_begin()) return false;
  const size_t frame_bytes = (size_t)kUiW * kUiH * sizeof(uint16_t);
  g_land = (uint16_t*)heap_caps_malloc(frame_bytes, MALLOC_CAP_SPIRAM);
  g_port = (uint16_t*)heap_caps_malloc(
      (size_t)board::kLcdNativeW * board::kLcdNativeH * sizeof(uint16_t),
      MALLOC_CAP_SPIRAM);
  if (g_land == nullptr || g_port == nullptr) return false;
  lv_init();
  lv_tick_set_cb((lv_tick_get_cb_t)millis);
  lv_display_t* disp = lv_display_create(kUiW, kUiH);
  lv_display_set_buffers(disp, g_land, nullptr, frame_bytes,
                         LV_DISPLAY_RENDER_MODE_DIRECT);
  lv_display_set_flush_cb(disp, flush_cb);
  return true;
}
void disp_backlight(bool on) { install_panel_backlight(on); }

#elif defined(BOARD_DISPLAY_RGB)
Display g_disp;
bool disp_begin() { return g_disp.begin(); }  // does lv_init + tick + flush
void disp_backlight(bool on) { g_disp.set_brightness(on ? 100 : 0); }
#endif

int ui_w() { return lv_display_get_horizontal_resolution(lv_display_get_default()); }
int ui_h() { return lv_display_get_vertical_resolution(lv_display_get_default()); }

void build_ui() {
  const int W = ui_w(), H = ui_h();
  lv_obj_t* scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0E1116), 0);

  g_title = lv_label_create(scr);
  lv_label_set_text(g_title, "Updating Apollo");
  lv_obj_set_style_text_color(g_title, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(g_title, &lv_font_montserrat_28, 0);
  lv_obj_align(g_title, LV_ALIGN_TOP_MID, 0, H / 4);

  g_bar = lv_bar_create(scr);
  lv_obj_set_size(g_bar, W * 6 / 10, 22);
  lv_bar_set_range(g_bar, 0, 100);
  lv_bar_set_value(g_bar, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(g_bar, lv_color_hex(0x22303C), 0);
  lv_obj_set_style_bg_color(g_bar, lv_color_hex(0x2E9BE6), LV_PART_INDICATOR);
  lv_obj_set_style_radius(g_bar, 11, 0);
  lv_obj_set_style_radius(g_bar, 11, LV_PART_INDICATOR);
  lv_obj_align(g_bar, LV_ALIGN_CENTER, 0, 0);

  g_pct = lv_label_create(scr);
  lv_obj_set_width(g_pct, 200);
  lv_obj_set_style_text_align(g_pct, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(g_pct, "0%");
  lv_obj_set_style_text_color(g_pct, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(g_pct, &lv_font_montserrat_28, 0);
  lv_obj_align(g_pct, LV_ALIGN_CENTER, 0, 50);

  g_status = lv_label_create(scr);
  lv_obj_set_width(g_status, W * 8 / 10);
  lv_obj_set_style_text_align(g_status, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(g_status, "Starting update...");
  lv_obj_set_style_text_color(g_status, lv_color_hex(0x8AA0B4), 0);
  lv_obj_set_style_text_font(g_status, &lv_font_montserrat_20, 0);
  lv_obj_align(g_status, LV_ALIGN_CENTER, 0, 110);
}

void ui_set(int pct, const char* status) {
  if (g_bar) lv_bar_set_value(g_bar, pct, LV_ANIM_OFF);
  if (g_pct) { char b[8]; std::snprintf(b, sizeof(b), "%d%%", pct); lv_label_set_text(g_pct, b); }
  if (status && g_status) lv_label_set_text(g_status, status);
  lv_timer_handler();
}

void show_warning() {
  lv_obj_add_flag(g_bar, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(g_pct, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(g_title, "Installing update");
  lv_obj_set_style_text_color(g_title, lv_color_hex(0xF2A33C), 0);
  lv_obj_align(g_title, LV_ALIGN_CENTER, 0, -70);
  lv_obj_set_style_text_color(g_status, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(g_status, &lv_font_montserrat_20, 0);
  lv_label_set_text(g_status,
                    "The screen will be dark for about 20 seconds.\n"
                    "Do NOT power off or unplug.\n"
                    "It will restart on its own when finished.");
  lv_obj_align(g_status, LV_ALIGN_CENTER, 0, 20);
  lv_timer_handler();
}

void fail_and_reboot(Config& config, const char* msg) {
  config.set_pending_install("");  // don't loop on a broken install
  if (g_status) { lv_label_set_text(g_status, msg); lv_timer_handler(); }
  core::logf("InstallMode: FAILED - %s\n", msg);
  delay(4000);
  esp_restart();
}

}  // namespace

void run(Config& config) {
  const std::string version = config.pending_install();
  if (version.empty()) return;
  core::logf("InstallMode: pending install of %s\n", version.c_str());

  if (!disp_begin()) {
    core::logf("InstallMode: display failed; clearing flag and rebooting\n");
    config.set_pending_install("");
    delay(500);
    esp_restart();
  }
  build_ui();
  lv_timer_handler();

  // --- WiFi (saved creds) ---
  ui_set(0, "Connecting to WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(config.wifi_ssid().c_str(), config.wifi_password().c_str());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 30000) {
    delay(150);
    lv_timer_handler();
  }
  if (WiFi.status() != WL_CONNECTED) { fail_and_reboot(config, "WiFi failed"); return; }
  core::logf("InstallMode: WiFi up, IP=%s\n", WiFi.localIP().toString().c_str());

  // --- Clock: inherited from the main session's RTC; NTP only as a failsafe
  //     (TLS cert dates need a real time). ---
  if (time(nullptr) < 1700000000) {
    ui_set(0, "Syncing time...");
    configTime(0, 0, "pool.ntp.org");
    t0 = millis();
    while (time(nullptr) < 1700000000 && millis() - t0 < 20000) {
      delay(150);
      lv_timer_handler();
    }
    if (time(nullptr) < 1700000000) { fail_and_reboot(config, "Time sync failed"); return; }
  }

  // --- PHASE 1: download the whole image into PSRAM (no flash writes) ---
  const std::string url = std::string(kSiteBase) + "/" + version +
                          "/firmware/app/" + board::kUpdateSlug + ".bin";
  core::logf("InstallMode: %s\n", url.c_str());
  ui_set(0, "Contacting update server...");
  esp_http_client_config_t http = {};
  http.url = url.c_str();
  http.crt_bundle_attach = esp_crt_bundle_attach;
  http.keep_alive_enable = true;
  http.timeout_ms = 20000;
  esp_http_client_handle_t c = esp_http_client_init(&http);
  if (c == nullptr || esp_http_client_open(c, 0) != ESP_OK) {
    fail_and_reboot(config, "Download failed"); return;
  }
  const int total = esp_http_client_fetch_headers(c);
  const int status = esp_http_client_get_status_code(c);
  if (status != 200 || total <= 0) {
    core::logf("InstallMode: HTTP %d len %d\n", status, total);
    esp_http_client_cleanup(c);
    fail_and_reboot(config, "Update not found for this board"); return;
  }
  uint8_t* img = (uint8_t*)heap_caps_malloc(total, MALLOC_CAP_SPIRAM);
  if (img == nullptr) { esp_http_client_cleanup(c); fail_and_reboot(config, "Out of memory"); return; }
  ui_set(0, "Downloading update...");

  // Cap each read so the bar actually climbs — otherwise the fast PSRAM
  // download returns the whole file in one or two reads and the % snaps 0->100.
  constexpr int kReadChunk = 32 * 1024;
  int got = 0, lastpct = -1;
  while (got < total) {
    const int want = (total - got < kReadChunk) ? (total - got) : kReadChunk;
    const int r = esp_http_client_read(c, (char*)img + got, want);
    if (r < 0) break;
    if (r == 0) { if (esp_http_client_is_complete_data_received(c)) break; else continue; }
    got += r;
    const int pct = got * 100 / total;
    if (pct != lastpct) { lastpct = pct; ui_set(pct, nullptr); }
  }
  esp_http_client_close(c);
  esp_http_client_cleanup(c);
  if (got != total) { fail_and_reboot(config, "Download interrupted"); return; }
  ui_set(100, "Download complete");
  delay(1200);

  // --- PHASE 2: PSRAM -> flash, screen blanked (flash writes glitch the panel) ---
  show_warning();
  delay(6000);
  disp_backlight(false);
  core::logf("InstallMode: writing %d bytes to flash (screen dark)\n", total);
  const esp_partition_t* part = esp_ota_get_next_update_partition(nullptr);
  esp_ota_handle_t ota = 0;
  esp_err_t e = esp_ota_begin(part, total, &ota);
  constexpr int kChunk = 64 * 1024;
  for (int off = 0; e == ESP_OK && off < total; off += kChunk) {
    const int n = (total - off < kChunk) ? (total - off) : kChunk;
    e = esp_ota_write(ota, img + off, n);
  }
  if (e == ESP_OK) e = esp_ota_end(ota);
  if (e == ESP_OK) e = esp_ota_set_boot_partition(part);
  if (e != ESP_OK) {
    disp_backlight(true);
    fail_and_reboot(config, "Install failed");
    return;
  }
  config.set_pending_install("");  // done — don't reinstall next boot
  core::logf("InstallMode: install complete, rebooting into %s\n", version.c_str());
  delay(200);
  esp_restart();
}

}  // namespace install_mode
}  // namespace platform

#else  // no display backend — install mode isn't offered; just clear the flag.
namespace platform {
namespace install_mode {
void run(Config& config) { config.set_pending_install(""); }
}  // namespace install_mode
}  // namespace platform
#endif
