// ============================================================================
// DLTEST — standalone OTA-download proof + UI base.  NOT the real firmware.
//
// Proves: a LIGHTWEIGHT display stack (minimal single-buffer DSI panel + LVGL,
// no use_dma2d / no async double-buffer) leaves enough contiguous internal-DMA
// (~100 KB, measured) for the hosted-SDIO bulk download to succeed — so we CAN
// show a real, live update UI during the download. This is the proving ground
// for the eventual real-firmware "drop to a light display for the update" path.
//
// P4-5 panel (HX8394 720x1280) init copied from board_config.h/display.cpp.
//   pio run -e esp32-p4-dltest -t upload && pio device monitor
// ============================================================================

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <time.h>
#include <cstring>

#include <lvgl.h>

#include <esp_cache.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_https_ota.h>
#include <esp_ldo_regulator.h>
#include <esp_lcd_mipi_dsi.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <mbedtls/platform.h>

namespace {

constexpr char kUrl[] =
    "https://mlsorensen.github.io/Apollo2/v0.10.0/firmware/app/"
    "p4-wifi6-touch-lcd-5.bin";

// Panel is native portrait 720x1280, mounted landscape → UI is 1280x720.
constexpr int kW = 720, kH = 1280;      // physical framebuffer
constexpr int kUiW = 1280, kUiH = 720;  // logical (rotated) UI
constexpr int kRst = 27;                // active HIGH
constexpr int kBl = 26;                 // LEDC PWM, normal polarity
constexpr long kDpiHz = 58000000;
constexpr int kLaneMbps = 700;

struct InitCmd { uint8_t cmd; const uint8_t* data; size_t len; int delay_ms; };
const InitCmd kInit[] = {
    {0x11, (uint8_t[]){0x00}, 0, 120}, {0x36, (uint8_t[]){0x00}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0}, {0xBA, (uint8_t[]){0x61}, 1, 0},
    {0xB9, (uint8_t[]){0xFF, 0x83, 0x94}, 3, 0},
    {0xB1, (uint8_t[]){0x48, 0x0A, 0x6A, 0x09, 0x33, 0x54, 0x71, 0x71, 0x2E, 0x45}, 10, 0},
    {0xBA, (uint8_t[]){0x61, 0x03, 0x68, 0x6B, 0xB2, 0xC0}, 6, 0},
    {0xB2, (uint8_t[]){0x00, 0x80, 0x64, 0x0C, 0x06, 0x2F}, 6, 0},
    {0xB4, (uint8_t[]){0x1C, 0x78, 0x1C, 0x78, 0x1C, 0x78, 0x01, 0x0C, 0x86, 0x75,
                       0x00, 0x3F, 0x1C, 0x78, 0x1C, 0x78, 0x1C, 0x78, 0x01, 0x0C, 0x86}, 21, 0},
    {0xD3, (uint8_t[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x08, 0x32, 0x10,
                       0x05, 0x00, 0x05, 0x32, 0x13, 0xC1, 0x00, 0x01, 0x32, 0x10,
                       0x08, 0x00, 0x00, 0x37, 0x03, 0x07, 0x07, 0x37, 0x05, 0x05,
                       0x37, 0x0C, 0x40}, 33, 0},
    {0xD5, (uint8_t[]){0x18, 0x18, 0x18, 0x18, 0x22, 0x23, 0x20, 0x21, 0x04, 0x05,
                       0x06, 0x07, 0x00, 0x01, 0x02, 0x03, 0x18, 0x18, 0x18, 0x18,
                       0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
                       0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
                       0x19, 0x19, 0x19, 0x19}, 44, 0},
    {0xD6, (uint8_t[]){0x18, 0x18, 0x19, 0x19, 0x21, 0x20, 0x23, 0x22, 0x03, 0x02,
                       0x01, 0x00, 0x07, 0x06, 0x05, 0x04, 0x18, 0x18, 0x18, 0x18,
                       0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
                       0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
                       0x19, 0x19, 0x18, 0x18}, 44, 0},
    {0xE0, (uint8_t[]){0x07, 0x08, 0x09, 0x0D, 0x10, 0x14, 0x16, 0x13, 0x24, 0x36,
                       0x48, 0x4A, 0x58, 0x6F, 0x76, 0x80, 0x97, 0xA5, 0xA8, 0xB5,
                       0xC6, 0x62, 0x63, 0x68, 0x6F, 0x72, 0x78, 0x7F, 0x7F, 0x00,
                       0x02, 0x08, 0x0D, 0x0C, 0x0E, 0x0F, 0x10, 0x24, 0x36, 0x48,
                       0x4A, 0x58, 0x6F, 0x78, 0x82, 0x99, 0xA4, 0xA0, 0xB1, 0xC0,
                       0x5E, 0x5E, 0x64, 0x6B, 0x6C, 0x73, 0x7F, 0x7F}, 58, 0},
    {0xCC, (uint8_t[]){0x0B}, 1, 0}, {0xC0, (uint8_t[]){0x1F, 0x73}, 2, 0},
    {0xB6, (uint8_t[]){0x6B, 0x6B}, 2, 0}, {0xD4, (uint8_t[]){0x02}, 1, 0},
    {0xBD, (uint8_t[]){0x01}, 1, 0}, {0xB1, (uint8_t[]){0x00}, 1, 0},
    {0xBD, (uint8_t[]){0x00}, 1, 0},
    {0xBF, (uint8_t[]){0x40, 0x81, 0x50, 0x00, 0x1A, 0xFC, 0x01}, 7, 0},
    {0x3A, (uint8_t[]){0x50}, 1, 0}, {0x11, (uint8_t[]){0x00}, 0, 200},
    {0xB2, (uint8_t[]){0x00, 0x80, 0x64, 0x0C, 0x06, 0x2F, 0x00, 0x00, 0x00, 0x00,
                       0xC0, 0x18}, 12, 0},
    {0x29, (uint8_t[]){0x00}, 0, 80},
};

esp_lcd_panel_handle_t g_panel = nullptr;
uint16_t* g_port = nullptr;   // portrait staging buffer (rotated frame) -> panel
uint16_t* g_land = nullptr;   // landscape full-frame LVGL render target
lv_obj_t* g_title = nullptr;
lv_obj_t* g_ver = nullptr;
lv_obj_t* g_bar = nullptr;
lv_obj_t* g_pct = nullptr;
lv_obj_t* g_status = nullptr;

void* psram_calloc(size_t n, size_t sz) {
  return heap_caps_calloc(n, sz, MALLOC_CAP_SPIRAM);
}

void log_heap(const char* tag) {
  Serial.printf(
      "[heap %-10s] int free=%7u largest=%7u | DMA free=%7u largest=%7u\n", tag,
      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA),
      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                 MALLOC_CAP_DMA));
}

bool panel_begin() {
  esp_ldo_channel_handle_t ldo = nullptr;
  esp_ldo_channel_config_t ldo_cfg = {};
  ldo_cfg.chan_id = 3;
  ldo_cfg.voltage_mv = 2500;
  if (esp_ldo_acquire_channel(&ldo_cfg, &ldo) != ESP_OK) return false;

  esp_lcd_dsi_bus_config_t bus_cfg = {};
  bus_cfg.bus_id = 0;
  bus_cfg.num_data_lanes = 2;
  bus_cfg.phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT;
  bus_cfg.lane_bit_rate_mbps = kLaneMbps;
  esp_lcd_dsi_bus_handle_t bus = nullptr;
  if (esp_lcd_new_dsi_bus(&bus_cfg, &bus) != ESP_OK) return false;

  pinMode(kRst, OUTPUT);
  digitalWrite(kRst, LOW); delay(10);
  digitalWrite(kRst, HIGH); delay(10);
  digitalWrite(kRst, LOW); delay(120);

  esp_lcd_dbi_io_config_t dbi_cfg = {};
  dbi_cfg.virtual_channel = 0;
  dbi_cfg.lcd_cmd_bits = 8;
  dbi_cfg.lcd_param_bits = 8;
  esp_lcd_panel_io_handle_t dbi = nullptr;
  if (esp_lcd_new_panel_io_dbi(bus, &dbi_cfg, &dbi) != ESP_OK) return false;
  for (const auto& c : kInit) {
    esp_lcd_panel_io_tx_param(dbi, c.cmd, c.data, c.len);
    if (c.delay_ms) delay(c.delay_ms);
  }

  esp_lcd_dpi_panel_config_t dpi = {};
  dpi.virtual_channel = 0;
  dpi.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
  dpi.dpi_clock_freq_mhz = kDpiHz / 1000000;
  dpi.pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565;
  dpi.num_fbs = 2;  // double buffer: flip at VSYNC, never write the scanned FB
  dpi.video_timing.h_size = kW;
  dpi.video_timing.v_size = kH;
  dpi.video_timing.hsync_pulse_width = 20;
  dpi.video_timing.hsync_back_porch = 20;
  dpi.video_timing.hsync_front_porch = 40;
  dpi.video_timing.vsync_pulse_width = 4;
  dpi.video_timing.vsync_back_porch = 10;
  dpi.video_timing.vsync_front_porch = 24;
  dpi.flags.use_dma2d = false;
  if (esp_lcd_new_panel_dpi(bus, &dpi, &g_panel) != ESP_OK ||
      esp_lcd_panel_init(g_panel) != ESP_OK)
    return false;

  ledcAttach(kBl, 5000, 8);
  ledcWrite(kBl, 255);
  return true;
}

// DIRECT render mode: LVGL keeps the whole current frame in g_land and calls
// flush per dirty area. We present only on the LAST area of a refresh: rotate
// the full landscape frame 90° CW into the portrait staging buffer, then
// draw_bitmap it — the DPI driver copies it into the OFF-SCREEN framebuffer and
// flips at the next VSYNC, so the scanned buffer is never mid-write (no tear).
// Rotation: landscape (lx,ly) -> portrait (px = kW-1-ly, py = lx).
void flush_cb(lv_display_t* d, const lv_area_t* /*a*/, uint8_t* /*px_map*/) {
  if (!lv_display_flush_is_last(d)) {
    lv_display_flush_ready(d);
    return;
  }
  for (int ly = 0; ly < kUiH; ++ly) {
    const int px = kW - 1 - ly;
    const uint16_t* srow = g_land + (size_t)ly * kUiW;
    for (int lx = 0; lx < kUiW; ++lx) {
      g_port[(size_t)lx * kW + px] = srow[lx];
    }
  }
  esp_lcd_panel_draw_bitmap(g_panel, 0, 0, kW, kH, g_port);
  lv_display_flush_ready(d);
}

void build_ui() {
  // ABSOLUTE positioning (no flex): a widget's update invalidates only that
  // widget's region, never re-flows the whole screen — so we never repaint the
  // full frame (which strobes on the single buffer).
  lv_obj_t* scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0E1116), 0);

  g_title = lv_label_create(scr);
  lv_label_set_text(g_title, "Updating Apollo");
  lv_obj_set_style_text_color(g_title, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(g_title, &lv_font_montserrat_36, 0);
  lv_obj_align(g_title, LV_ALIGN_TOP_MID, 0, 170);

  g_ver = lv_label_create(scr);
  lv_label_set_text(g_ver, "v0.10.0");
  lv_obj_set_style_text_color(g_ver, lv_color_hex(0x8AA0B4), 0);
  lv_obj_set_style_text_font(g_ver, &lv_font_montserrat_20, 0);
  lv_obj_align(g_ver, LV_ALIGN_TOP_MID, 0, 240);

  g_bar = lv_bar_create(scr);
  lv_obj_set_size(g_bar, 760, 24);
  lv_bar_set_range(g_bar, 0, 100);
  lv_bar_set_value(g_bar, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(g_bar, lv_color_hex(0x22303C), 0);
  lv_obj_set_style_bg_color(g_bar, lv_color_hex(0x2E9BE6), LV_PART_INDICATOR);
  lv_obj_set_style_radius(g_bar, 12, 0);
  lv_obj_set_style_radius(g_bar, 12, LV_PART_INDICATOR);
  lv_obj_align(g_bar, LV_ALIGN_TOP_MID, 0, 320);

  // Fixed-width, center-aligned % text so its width changes (9%→100%) don't
  // shift anything around it.
  g_pct = lv_label_create(scr);
  lv_obj_set_width(g_pct, 200);
  lv_obj_set_style_text_align(g_pct, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(g_pct, "0%");
  lv_obj_set_style_text_color(g_pct, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(g_pct, &lv_font_montserrat_28, 0);
  lv_obj_align(g_pct, LV_ALIGN_TOP_MID, 0, 380);

  g_status = lv_label_create(scr);
  lv_obj_set_width(g_status, 900);
  lv_obj_set_style_text_align(g_status, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(g_status, "Downloading update...");
  lv_obj_set_style_text_color(g_status, lv_color_hex(0x8AA0B4), 0);
  lv_obj_set_style_text_font(g_status, &lv_font_montserrat_20, 0);
  lv_obj_align(g_status, LV_ALIGN_TOP_MID, 0, 450);
}

void ui_set(int pct, const char* status) {
  if (g_bar) lv_bar_set_value(g_bar, pct, LV_ANIM_OFF);
  if (g_pct) { char b[8]; std::snprintf(b, sizeof(b), "%d%%", pct); lv_label_set_text(g_pct, b); }
  if (status && g_status) lv_label_set_text(g_status, status);
  lv_timer_handler();
}

// Dramatic, distinct install screen — hide the download widgets, amber warning
// header, big "don't power off" body. Shown before the screen goes dark.
void show_install_warning() {
  lv_obj_add_flag(g_ver, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(g_bar, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(g_pct, LV_OBJ_FLAG_HIDDEN);

  lv_label_set_text(g_title, "Installing update");
  lv_obj_set_style_text_color(g_title, lv_color_hex(0xF2A33C), 0);  // amber
  lv_obj_set_style_text_font(g_title, &lv_font_montserrat_40, 0);
  lv_obj_align(g_title, LV_ALIGN_TOP_MID, 0, 210);

  lv_obj_set_style_text_color(g_status, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(g_status, &lv_font_montserrat_28, 0);
  lv_label_set_text(g_status,
                    "The screen will be dark for about 20 seconds.\n"
                    "Do NOT power off or unplug.\n"
                    "It will restart on its own when finished.");
  lv_obj_align(g_status, LV_ALIGN_TOP_MID, 0, 320);
  lv_timer_handler();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n===== DLTEST: OTA download WITH LVGL update UI =====");
  log_heap("boot");

  if (!panel_begin()) { Serial.println("PANEL FAILED"); return; }
  Serial.println("panel up");
  log_heap("panel-up");

  // Full-frame buffers: g_land is LVGL's DIRECT-mode render target (landscape);
  // g_port is the rotated portrait frame handed to the panel. Both PSRAM.
  const size_t frame_bytes = (size_t)kUiW * kUiH * sizeof(uint16_t);
  g_land = (uint16_t*)heap_caps_malloc(frame_bytes, MALLOC_CAP_SPIRAM);
  g_port = (uint16_t*)heap_caps_malloc((size_t)kW * kH * sizeof(uint16_t),
                                       MALLOC_CAP_SPIRAM);
  lv_init();
  lv_tick_set_cb((lv_tick_get_cb_t)millis);
  static lv_display_t* disp = lv_display_create(kUiW, kUiH);
  lv_display_set_buffers(disp, g_land, nullptr, frame_bytes,
                         LV_DISPLAY_RENDER_MODE_DIRECT);
  lv_display_set_flush_cb(disp, flush_cb);
  build_ui();
  lv_timer_handler();
  Serial.println("lvgl up");
  log_heap("lvgl-up");

#ifdef DLTEST_SIM
  // Isolation: animate progress with NO WiFi/download, to see if the strobe is
  // the display present itself or contention with the radio.
  Serial.println("SIM: animating progress only (no WiFi/download)");
  for (int pct = 0; pct <= 100; ++pct) {
    ui_set(pct, "Simulated download...");
    delay(250);
  }
  ui_set(100, "Update complete");
  return;
#endif

  Preferences p;
  p.begin("micra", true);
  String ssid = p.getString("ssid", "");
  String pass = p.getString("wifipass", "");
  p.end();
  if (ssid.isEmpty()) { ui_set(0, "No saved WiFi"); return; }

  ui_set(0, "Connecting to WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 30000) { delay(150); lv_timer_handler(); }
  if (WiFi.status() != WL_CONNECTED) { ui_set(0, "WiFi failed"); return; }
  Serial.printf("WiFi up, IP=%s\n", WiFi.localIP().toString().c_str());
  log_heap("wifi-up");

  ui_set(0, "Syncing time...");
  configTime(0, 0, "pool.ntp.org");
  t0 = millis();
  time_t now = 0;
  while ((now = time(nullptr)) < 1700000000 && millis() - t0 < 20000) { delay(150); lv_timer_handler(); }
  if (now < 1700000000) { ui_set(0, "Time sync failed"); return; }
  log_heap("ntp-up");

  // TLS records in INTERNAL RAM (default) — testing whether keeping TLS off
  // PSRAM reduces the display-vs-download contention strobe.
  // mbedtls_platform_set_calloc_free(psram_calloc, free);
  (void)psram_calloc;

  // ===== PHASE 1: download the whole image into PSRAM (NO flash writes) =====
  // Clean live progress — the strobe only comes from flash writes, and there
  // are none here.
  // "Contacting server" covers the TLS handshake (a couple seconds at 0% that
  // otherwise looks stuck); flip to the download bar once bytes actually flow.
  ui_set(0, "Contacting update server...");
  Serial.printf("Phase 1: download -> PSRAM: %s\n", kUrl);
  esp_http_client_config_t http = {};
  http.url = kUrl;
  http.crt_bundle_attach = esp_crt_bundle_attach;
  http.keep_alive_enable = true;
  http.timeout_ms = 20000;
  esp_http_client_handle_t c = esp_http_client_init(&http);
  if (c == nullptr || esp_http_client_open(c, 0) != ESP_OK) {
    ui_set(0, "Download failed"); return;
  }
  const int total = esp_http_client_fetch_headers(c);  // Content-Length
  if (total <= 0) { ui_set(0, "Download failed"); esp_http_client_cleanup(c); return; }
  uint8_t* img = (uint8_t*)heap_caps_malloc(total, MALLOC_CAP_SPIRAM);
  if (img == nullptr) { ui_set(0, "Out of memory"); return; }
  Serial.printf("image = %d bytes, buffering in PSRAM\n", total);
  ui_set(0, "Downloading update...");

  const uint32_t t_dl = millis();
  int got = 0, last = -1;
  while (got < total) {
    const int r = esp_http_client_read(c, (char*)img + got, total - got);
    if (r < 0) break;
    if (r == 0) { if (esp_http_client_is_complete_data_received(c)) break; else continue; }
    got += r;
    const int pct = got * 100 / total;
    if (pct != last) {
      last = pct;
      ui_set(pct, nullptr);
      if (pct % 10 == 0) Serial.printf("  dl %3d%%\n", pct);
    }
  }
  esp_http_client_close(c);
  esp_http_client_cleanup(c);
  if (got != total) { ui_set(last < 0 ? 0 : last, "Download failed"); return; }
  Serial.printf("Phase 1 done: %d bytes in %lus\n", got,
                (unsigned long)((millis() - t_dl) / 1000));
  // The PSRAM download is so fast the bar jumps to 100 — hold on a completed
  // state briefly so it reads as "done" before the install screen.
  ui_set(100, "Download complete");
  delay(1200);

  // ===== PHASE 2: PSRAM -> flash, hidden in the restart =====
  // Flash writes strobe the panel no matter what, so blank the screen (this is
  // the restart anyway) and write while dark. WARN clearly first — the dark
  // stretch is ~15-20 s and the user must not power off.
  show_install_warning();
  delay(6000);          // give the warning real read time
  ledcWrite(kBl, 0);    // backlight OFF — the flash-write glitch is invisible
  Serial.println("Phase 2: PSRAM -> flash (screen dark)");
  const uint32_t t_fl = millis();
  const esp_partition_t* part = esp_ota_get_next_update_partition(nullptr);
  esp_ota_handle_t ota = 0;
  esp_err_t e = esp_ota_begin(part, total, &ota);
  constexpr int kFlashChunk = 64 * 1024;  // bigger writes -> less per-call overhead
  for (int off = 0; e == ESP_OK && off < total; off += kFlashChunk) {
    const int n = (total - off < kFlashChunk) ? (total - off) : kFlashChunk;
    e = esp_ota_write(ota, img + off, n);
  }
  if (e == ESP_OK) e = esp_ota_end(ota);
  if (e == ESP_OK) e = esp_ota_set_boot_partition(part);
  Serial.printf("Phase 2 done in %lus, err=%s\n",
                (unsigned long)((millis() - t_fl) / 1000), esp_err_to_name(e));
  if (e != ESP_OK) { ledcWrite(kBl, 255); ui_set(100, "Install failed"); return; }
  delay(200);
  esp_restart();  // boots the freshly written image
}

void loop() { lv_timer_handler(); delay(5); }
