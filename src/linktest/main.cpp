// Apollo Link bench tester — a standalone firmware for the P4-5 that exercises
// one Link board at a time: shows both paddle-sense inputs live, drives the
// shot line and CTRL from on-screen buttons, and walks an operator through a
// guided meter test. Powered from the Link's own USB-C (nothing on the P4) so
// the 5 V pass-through is proven by booting at all; the rail readout shows
// the number. Serial (UART, 115200) mirrors every state change and accepts
// the same commands, so a test can be driven from a host while the operator
// holds the meter — but nothing here needs a cable.
//
// Deliberately NOT the shipping firmware: no NVS, no per-unit pin overrides,
// no radio, no brew logic. Pins come from board_config.h only.
//
//   make build-linktest / make flash-linktest   (P4-5; rev3 sibling auto-picked)
//
// Boot state is CTRL OFF + SHOT OFF (the Link in copper), the opposite of the
// shipping firmware, so the first meter step can be taken straight away.

#include <Arduino.h>
#include <lvgl.h>

#include <driver/gpio.h>

#include <cstdio>
#include <cstring>

#include "core/system.h"
#include "platform_esp32/board_config.h"
#include "platform_esp32/display.h"
#include "platform_esp32/log_setup.h"
#include "platform_esp32/touch.h"
#include "version.h"

namespace {

// ---------------------------------------------------------------------------
// Pins (board constants only)
constexpr int kLinkDrive  = board::kLinkDrivePin;   // GPIO3  -> AQY212EH LED
constexpr int kLinkSense  = board::kLinkSensePin;   // GPIO5  <- paddle via the NO contact
constexpr int kLinkCtrl   = board::kLinkCtrlPin;    // GPIO4  -> the four LH1502 LEDs
constexpr int kCornerDrive = board::kPaddleDrivePin;  // GPIO52 (opto cable)
constexpr int kCornerSense = board::kPaddleSensePin;  // GPIO51
static_assert(kLinkDrive >= 0 && kLinkSense >= 0 && kLinkCtrl >= 0,
              "linktest needs a board with the Apollo Link pin set");

// ---------------------------------------------------------------------------
// State
platform::Display g_display;
platform::Touch g_touch;

bool g_drv = false;    // SHOT line (both drive pins)
bool g_ctrl = false;   // CTRL
uint32_t g_pulse_until_ms = 0;

struct Sense {
  int pin;
  const char* name;
  bool raw_low = false;     // last raw sample
  bool stable_low = false;  // debounced
  uint8_t run = 0;          // consecutive samples agreeing with raw_low
  uint32_t edges = 0;
  uint32_t last_edge_ms = 0;
};
Sense g_link{kLinkSense, "LINK"};
Sense g_corner{kCornerSense, "CORNER"};
constexpr uint8_t kStablePolls = 4;  // 4 x 5 ms

float g_rail_v = 0.0f;

// ---------------------------------------------------------------------------
// Serial
void say(const char* fmt, ...) {
  char buf[200];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.print("link: ");
  Serial.println(buf);
}

// ---------------------------------------------------------------------------
// Outputs
void apply_drv(bool on) {
  g_drv = on;
  digitalWrite(kLinkDrive, on ? HIGH : LOW);
  if (kCornerDrive >= 0)
    digitalWrite(kCornerDrive, (board::kPaddleActiveHigh ? on : !on) ? HIGH : LOW);
}
void apply_ctrl(bool on) {
  g_ctrl = on;
  digitalWrite(kLinkCtrl, on ? HIGH : LOW);
}

// ---------------------------------------------------------------------------
// UI
lv_obj_t* g_rail_lbl = nullptr;
lv_obj_t* g_up_lbl = nullptr;
lv_obj_t* g_link_state = nullptr;
lv_obj_t* g_link_sub = nullptr;
lv_obj_t* g_corner_state = nullptr;
lv_obj_t* g_corner_sub = nullptr;
lv_obj_t* g_drv_btn = nullptr;
lv_obj_t* g_drv_lbl = nullptr;
lv_obj_t* g_ctrl_btn = nullptr;
lv_obj_t* g_ctrl_lbl = nullptr;
lv_obj_t* g_status_lbl = nullptr;

// Guided test overlay
lv_obj_t* g_test = nullptr;        // full-screen container (hidden when idle)
lv_obj_t* g_test_title = nullptr;
lv_obj_t* g_test_body = nullptr;
lv_obj_t* g_test_expect = nullptr;
lv_obj_t* g_test_live = nullptr;
lv_obj_t* g_test_pass = nullptr;
lv_obj_t* g_test_fail = nullptr;
lv_obj_t* g_test_abort = nullptr;
lv_obj_t* g_test_pass_lbl = nullptr;
lv_obj_t* g_test_abort_lbl = nullptr;

constexpr uint32_t kBg = 0x0E1116, kCard = 0x1B2230, kText = 0xE6EDF3, kMuted = 0x8AA0B4;
constexpr uint32_t kGreen = 0x2EA043, kRed = 0xDA3633, kAmber = 0xD29922, kBlue = 0x1F6FEB;

lv_obj_t* make_card(lv_obj_t* parent, int x, int y, int w, int h, uint32_t bg) {
  lv_obj_t* c = lv_obj_create(parent);
  lv_obj_set_pos(c, x, y);
  lv_obj_set_size(c, w, h);
  lv_obj_set_style_bg_color(c, lv_color_hex(bg), 0);
  lv_obj_set_style_border_width(c, 0, 0);
  lv_obj_set_style_radius(c, 16, 0);
  lv_obj_set_style_pad_all(c, 16, 0);
  lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  return c;
}
lv_obj_t* make_label(lv_obj_t* parent, const char* txt, const lv_font_t* font, uint32_t color) {
  lv_obj_t* l = lv_label_create(parent);
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  return l;
}
lv_obj_t* make_button(lv_obj_t* parent, int x, int y, int w, int h, const char* txt,
                      uint32_t bg, lv_event_cb_t cb, lv_obj_t** lbl_out) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
  lv_obj_set_style_radius(b, 16, 0);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* l = make_label(b, txt, &lv_font_montserrat_32, kText);
  lv_obj_center(l);
  if (lbl_out) *lbl_out = l;
  return b;
}

void set_status(const char* s) {
  if (g_status_lbl) lv_label_set_text(g_status_lbl, s);
}

void refresh_controls() {
  lv_label_set_text(g_drv_lbl, g_drv ? "SHOT: ON" : "SHOT: OFF");
  lv_obj_set_style_bg_color(g_drv_btn, lv_color_hex(g_drv ? kRed : kCard), 0);
  lv_label_set_text(g_ctrl_lbl, g_ctrl ? "CTRL: ON" : "CTRL: OFF");
  lv_obj_set_style_bg_color(g_ctrl_btn, lv_color_hex(g_ctrl ? kBlue : kCard), 0);
}

void refresh_sense_tile(const Sense& s, lv_obj_t* state, lv_obj_t* sub) {
  lv_label_set_text(state, s.stable_low ? "CLOSED" : "OPEN");
  lv_obj_set_style_text_color(state, lv_color_hex(s.stable_low ? kGreen : kMuted), 0);
  char b[64];
  snprintf(b, sizeof(b), "GPIO%d  raw %s   edges %lu", s.pin, s.raw_low ? "LOW" : "HIGH",
           static_cast<unsigned long>(s.edges));
  lv_label_set_text(sub, b);
}

// ---------------------------------------------------------------------------
// Guided test
enum class Kind { kManual, kCheckOpenOnPass, kCheckClosedOnPass, kAutoClosed, kAutoOpen };
struct Step {
  const char* title;
  const char* action;  // DO
  const char* check;   // CHECK
  Kind kind;           // THEN is derived from the kind, so every step says what to tap
  bool ctrl;
  bool drv;
};
// The rig: a continuity meter across J2 MW and MB (what the Micra sees) and
// a short across J2 P1 and P2 standing in for the paddle. The meter never
// moves. Each step applies its CTRL/SHOT state before it is shown.
const Step kSteps[] = {
    {"CTRL off: paddle in copper",
     "Short P1 to P2 and hold it.",
     "Meter CLOSED (beep). LED dark.",
     Kind::kCheckOpenOnPass, false, false},
    {"CTRL off: copper opens",
     "Remove the short.",
     "Meter OPEN.",
     Kind::kManual, false, false},
    {"CTRL on: Apollo takes the paddle",
     "Leave P1 and P2 open.",
     "LED lit. Meter OPEN.",
     Kind::kManual, true, false},
    {"CTRL on: paddle reaches Apollo",
     "Short P1 to P2.",
     "The tester sees the paddle: LINK reads CLOSED.",
     Kind::kAutoClosed, true, false},
    {"CTRL on: Micra no longer sees the paddle",
     "Keep the short on.",
     "Meter OPEN while shorted.",
     Kind::kCheckClosedOnPass, true, false},
    {"CTRL on: paddle releases",
     "Remove the short.",
     "LINK reads OPEN again.",
     Kind::kAutoOpen, true, false},
    {"SHOT on: Apollo closes the Micra line",
     "Leave P1 and P2 open.",
     "Meter CLOSED (beep).",
     Kind::kManual, true, true},
    {"SHOT off: Micra line opens",
     "Nothing to do.",
     "Meter OPEN.",
     Kind::kManual, true, false},
};
const char* then_text(Kind k) {
  switch (k) {
    case Kind::kManual:            return "Tap PASS if that is what you see, FAIL if not.";
    case Kind::kCheckOpenOnPass:   return "Still holding the short: tap PASS if that is what you see, FAIL if not. (The tester also checks LINK is OPEN.)";
    case Kind::kCheckClosedOnPass: return "Still holding the short: tap PASS if that is what you see, FAIL if not. (The tester also checks LINK is CLOSED.)";
    case Kind::kAutoClosed:
    case Kind::kAutoOpen:          return "Advances by itself when the tester sees it. Nothing to tap. Tap FAIL if nothing happens within a minute.";
  }
  return "";
}
constexpr int kStepCount = sizeof(kSteps) / sizeof(kSteps[0]);
constexpr uint32_t kAutoTimeoutMs = 60000;

bool g_testing = false;
int g_step = -1;        // -1 idle; kStepCount = summary
bool g_result[kStepCount];
int g_board = 1;
uint32_t g_step_started_ms = 0;

void test_show_step();
void test_summary();

void test_start() {
  g_testing = true;
  g_step = 0;
  for (bool& r : g_result) r = false;
  lv_obj_remove_flag(g_test, LV_OBJ_FLAG_HIDDEN);
  say("TEST board=%d start", g_board);
  test_show_step();
}

void test_finish_step(bool pass) {
  g_result[g_step] = pass;
  say("TEST board=%d step=%d \"%s\" %s", g_board, g_step + 1, kSteps[g_step].title,
      pass ? "PASS" : "FAIL");
  ++g_step;
  if (g_step >= kStepCount) {
    test_summary();
  } else {
    test_show_step();
  }
}

void test_end() {
  g_testing = false;
  g_step = -1;
  apply_ctrl(false);
  apply_drv(false);
  refresh_controls();
  lv_obj_add_flag(g_test, LV_OBJ_FLAG_HIDDEN);
}

void test_show_step() {
  const Step& st = kSteps[g_step];
  apply_ctrl(st.ctrl);
  apply_drv(st.drv);
  refresh_controls();
  g_step_started_ms = millis();
  char t[96];
  snprintf(t, sizeof(t), "Board %d  -  step %d of %d:  %s", g_board, g_step + 1, kStepCount, st.title);
  lv_label_set_text(g_test_title, t);
  char body[300];
  snprintf(body, sizeof(body), "DO:  %s\n\nCHECK:  %s", st.action, st.check);
  lv_label_set_text(g_test_body, body);
  char then[200];
  snprintf(then, sizeof(then), "THEN:  %s", then_text(st.kind));
  lv_label_set_text(g_test_expect, then);
  lv_obj_set_style_text_color(g_test_expect, lv_color_hex(kAmber), 0);
  const bool manual = st.kind == Kind::kManual || st.kind == Kind::kCheckOpenOnPass ||
                      st.kind == Kind::kCheckClosedOnPass;
  if (manual) {
    lv_obj_remove_flag(g_test_pass, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(g_test_pass, LV_OBJ_FLAG_HIDDEN);  // the firmware decides
  }
  lv_label_set_text(g_test_pass_lbl, "PASS");
  lv_obj_remove_flag(g_test_fail, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(g_test_abort_lbl, "ABORT");
  say("TEST board=%d step=%d \"%s\" ctrl=%d shot=%d -- DO: %s CHECK: %s THEN: %s", g_board,
      g_step + 1, st.title, st.ctrl, st.drv, st.action, st.check, then_text(st.kind));
}

void test_summary() {
  int fails = 0;
  for (bool r : g_result) fails += r ? 0 : 1;
  char t[96];
  snprintf(t, sizeof(t), "Board %d  -  %s", g_board, fails == 0 ? "PASS" : "FAIL");
  lv_label_set_text(g_test_title, t);
  char body[400];
  size_t n = 0;
  if (fails == 0) {
    n += snprintf(body + n, sizeof(body) - n, "All %d steps passed.", kStepCount);
  } else {
    n += snprintf(body + n, sizeof(body) - n, "Failed:");
    for (int i = 0; i < kStepCount && n < sizeof(body) - 1; ++i)
      if (!g_result[i]) n += snprintf(body + n, sizeof(body) - n, "\n  %d. %s", i + 1, kSteps[i].title);
  }
  lv_label_set_text(g_test_body, body);
  lv_label_set_text(g_test_expect, "NEXT BOARD starts over for the next Link; DONE returns to the panel.");
  lv_obj_set_style_text_color(g_test_expect, lv_color_hex(fails == 0 ? kGreen : kRed), 0);
  lv_obj_remove_flag(g_test_pass, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(g_test_pass_lbl, "NEXT BOARD");
  lv_obj_add_flag(g_test_fail, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(g_test_abort_lbl, "DONE");
  say("TEST board=%d RESULT %s (%d/%d passed)", g_board, fails == 0 ? "PASS" : "FAIL",
      kStepCount - fails, kStepCount);
  apply_ctrl(false);
  apply_drv(false);
  refresh_controls();
}

// PASS button / "pass" command
void test_pass() {
  if (!g_testing) return;
  if (g_step >= kStepCount) {  // summary: next board
    ++g_board;
    test_start();
    return;
  }
  const Step& st = kSteps[g_step];
  if (st.kind == Kind::kCheckOpenOnPass) {
    test_finish_step(!g_link.stable_low);
  } else if (st.kind == Kind::kCheckClosedOnPass) {
    test_finish_step(g_link.stable_low);
  } else if (st.kind == Kind::kManual) {
    test_finish_step(true);
  }  // auto steps ignore PASS: the pin decides
}
void test_fail() {
  if (!g_testing || g_step >= kStepCount) return;
  test_finish_step(false);
}
void test_abort() {
  if (!g_testing) return;
  say("TEST board=%d %s", g_board, g_step >= kStepCount ? "done" : "ABORTED");
  test_end();
}

// Auto steps + live line, called from loop()
void test_poll() {
  if (!g_testing || g_step < 0 || g_step >= kStepCount) return;
  const Step& st = kSteps[g_step];
  char live[96];
  const uint32_t elapsed = millis() - g_step_started_ms;
  snprintf(live, sizeof(live), "live: LINK %s   CORNER %s   CTRL %s   SHOT %s   %lus",
           g_link.stable_low ? "CLOSED" : "OPEN", g_corner.stable_low ? "CLOSED" : "OPEN",
           g_ctrl ? "on" : "off", g_drv ? "on" : "off", static_cast<unsigned long>(elapsed / 1000));
  lv_label_set_text(g_test_live, live);
  if (st.kind == Kind::kAutoClosed && g_link.stable_low) {
    test_finish_step(true);
  } else if (st.kind == Kind::kAutoOpen && !g_link.stable_low && elapsed > 500) {
    test_finish_step(true);
  } else if ((st.kind == Kind::kAutoClosed || st.kind == Kind::kAutoOpen) && elapsed > kAutoTimeoutMs) {
    say("TEST step %d timed out", g_step + 1);
    test_finish_step(false);
  }
}

// ---------------------------------------------------------------------------
// Button callbacks
void on_drv(lv_event_t*) {
  apply_drv(!g_drv);
  refresh_controls();
  say("shot=%d (GPIO%d%s)", g_drv, kLinkDrive, kCornerDrive >= 0 ? " + corner" : "");
}
void on_ctrl(lv_event_t*) {
  apply_ctrl(!g_ctrl);
  refresh_controls();
  say("ctrl=%d (GPIO%d)", g_ctrl, kLinkCtrl);
}
void on_pulse(lv_event_t*) {
  apply_drv(true);
  refresh_controls();
  g_pulse_until_ms = millis() + 500;
  say("pulse 500 ms");
}
void on_reset(lv_event_t*) {
  g_link.edges = g_corner.edges = 0;
  say("counters reset");
}
void on_test(lv_event_t*) { test_start(); }
void on_pass(lv_event_t*) { test_pass(); }
void on_fail(lv_event_t*) { test_fail(); }
void on_abort(lv_event_t*) { test_abort(); }

void build_ui() {
  lv_obj_t* scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(kBg), 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  const int W = g_display.width();

  lv_obj_t* title = make_label(scr, "APOLLO LINK TESTER", &lv_font_montserrat_36, kText);
  lv_obj_set_pos(title, 40, 24);
  char v[64];
  snprintf(v, sizeof(v), "fw %s", fw::kVersion);
  lv_obj_t* ver = make_label(scr, v, &lv_font_montserrat_20, kMuted);
  lv_obj_set_pos(ver, 44, 70);
  g_rail_lbl = make_label(scr, "bat node -- V", &lv_font_montserrat_24, kText);
  lv_obj_align(g_rail_lbl, LV_ALIGN_TOP_RIGHT, -40, 28);
  g_up_lbl = make_label(scr, "up 0:00", &lv_font_montserrat_20, kMuted);
  lv_obj_align(g_up_lbl, LV_ALIGN_TOP_RIGHT, -40, 70);

  // Sense tiles
  const int tile_w = (W - 40 * 3) / 2;
  lv_obj_t* lt = make_card(scr, 40, 110, tile_w, 220, kCard);
  lv_obj_t* lh = make_label(lt, "LINK sense (J1 SENSE)", &lv_font_montserrat_24, kMuted);
  lv_obj_set_pos(lh, 0, 0);
  g_link_state = make_label(lt, "OPEN", &lv_font_montserrat_48, kMuted);
  lv_obj_align(g_link_state, LV_ALIGN_CENTER, 0, 8);
  g_link_sub = make_label(lt, "", &lv_font_montserrat_20, kMuted);
  lv_obj_align(g_link_sub, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  lv_obj_t* ct = make_card(scr, 40 * 2 + tile_w, 110, tile_w, 220, kCard);
  lv_obj_t* ch = make_label(ct, "CORNER sense (opto cable)", &lv_font_montserrat_24, kMuted);
  lv_obj_set_pos(ch, 0, 0);
  g_corner_state = make_label(ct, "OPEN", &lv_font_montserrat_48, kMuted);
  lv_obj_align(g_corner_state, LV_ALIGN_CENTER, 0, 8);
  g_corner_sub = make_label(ct, "", &lv_font_montserrat_20, kMuted);
  lv_obj_align(g_corner_sub, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  // Controls
  const int bw = (W - 40 * 4) / 3;
  g_drv_btn = make_button(scr, 40, 360, bw, 110, "SHOT: OFF", kCard, on_drv, &g_drv_lbl);
  g_ctrl_btn = make_button(scr, 40 * 2 + bw, 360, bw, 110, "CTRL: OFF", kCard, on_ctrl, &g_ctrl_lbl);
  make_button(scr, 40 * 3 + bw * 2, 360, bw, 110, "PULSE 500 ms", kCard, on_pulse, nullptr);
  make_button(scr, 40, 500, bw * 2 + 40, 110, "GUIDED TEST", kGreen, on_test, nullptr);
  make_button(scr, 40 * 3 + bw * 2, 500, bw, 110, "RESET COUNTS", kCard, on_reset, nullptr);

  g_status_lbl = make_label(scr, "CTRL off = Link in copper. Serial: drv/ctrl/pulse/status/test/pass/fail/abort",
                            &lv_font_montserrat_20, kMuted);
  lv_obj_align(g_status_lbl, LV_ALIGN_BOTTOM_LEFT, 40, -24);

  // Guided test overlay
  g_test = make_card(scr, 0, 0, W, g_display.height(), kBg);
  lv_obj_set_style_radius(g_test, 0, 0);
  lv_obj_set_style_pad_all(g_test, 40, 0);
  g_test_title = make_label(g_test, "", &lv_font_montserrat_32, kText);
  lv_obj_set_pos(g_test_title, 0, 0);
  g_test_body = make_label(g_test, "", &lv_font_montserrat_32, kText);
  lv_obj_set_width(g_test_body, W - 80);
  lv_label_set_long_mode(g_test_body, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(g_test_body, 0, 90);
  g_test_expect = make_label(g_test, "", &lv_font_montserrat_28, kAmber);
  lv_obj_set_width(g_test_expect, W - 80);
  lv_label_set_long_mode(g_test_expect, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(g_test_expect, 0, 300);
  g_test_live = make_label(g_test, "", &lv_font_montserrat_24, kMuted);
  lv_obj_set_pos(g_test_live, 0, 440);
  const int tb = (W - 80 - 40 * 2) / 3;
  g_test_pass = make_button(g_test, 0, 520, tb, 120, "PASS", kGreen, on_pass, &g_test_pass_lbl);
  g_test_fail = make_button(g_test, tb + 40, 520, tb, 120, "FAIL", kRed, on_fail, nullptr);
  g_test_abort = make_button(g_test, (tb + 40) * 2, 520, tb, 120, "ABORT", kCard, on_abort, &g_test_abort_lbl);
  lv_obj_add_flag(g_test, LV_OBJ_FLAG_HIDDEN);

  refresh_controls();
}

// ---------------------------------------------------------------------------
// Polling
void poll_sense(Sense& s) {
  if (s.pin < 0) return;
  const bool low = digitalRead(s.pin) == LOW;
  if (low == s.raw_low) {
    if (s.run < 255) ++s.run;
  } else {
    s.raw_low = low;
    s.run = 1;
  }
  if (s.run >= kStablePolls && s.stable_low != s.raw_low) {
    s.stable_low = s.raw_low;
    ++s.edges;
    s.last_edge_ms = millis();
    say("%s sense %s (GPIO%d, edges=%lu)", s.name, s.stable_low ? "CLOSED" : "OPEN", s.pin,
        static_cast<unsigned long>(s.edges));
  }
}

void read_rail() {
  if (board::kBatteryAdc < 0) return;
  uint32_t mv = 0;
  for (int i = 0; i < 8; ++i) mv += analogReadMilliVolts(board::kBatteryAdc);
  g_rail_v = (mv / 8) / 1000.0f * board::kBatteryDivider;
}

void print_status() {
  say("status ctrl=%d shot=%d link=%s(raw %s, edges %lu) corner=%s(raw %s, edges %lu) batnode=%.2fV(%s) up=%lus test=%s",
      g_ctrl, g_drv, g_link.stable_low ? "CLOSED" : "OPEN", g_link.raw_low ? "LOW" : "HIGH",
      static_cast<unsigned long>(g_link.edges), g_corner.stable_low ? "CLOSED" : "OPEN",
      g_corner.raw_low ? "LOW" : "HIGH", static_cast<unsigned long>(g_corner.edges), g_rail_v,
      g_rail_v >= 2.5f ? "USB" : "header", static_cast<unsigned long>(millis() / 1000),
      !g_testing ? "idle" : (g_step >= kStepCount ? "summary" : "running"));
}

void handle_command(char* line) {
  // trim
  while (*line == ' ') ++line;
  char* end = line + strlen(line);
  while (end > line && (end[-1] == ' ' || end[-1] == '\r' || end[-1] == '\n')) *--end = 0;
  if (!*line) return;
  int v = -1;
  if (sscanf(line, "drv %d", &v) == 1 || sscanf(line, "shot %d", &v) == 1) {
    apply_drv(v != 0); refresh_controls(); say("shot=%d", g_drv);
  } else if (sscanf(line, "ctrl %d", &v) == 1) {
    apply_ctrl(v != 0); refresh_controls(); say("ctrl=%d", g_ctrl);
  } else if (!strcmp(line, "pulse")) {
    on_pulse(nullptr);
  } else if (!strcmp(line, "status")) {
    print_status();
  } else if (!strcmp(line, "test")) {
    if (!g_testing) test_start(); else say("test already running (step %d)", g_step + 1);
  } else if (!strcmp(line, "pass")) {
    test_pass();
  } else if (!strcmp(line, "fail")) {
    test_fail();
  } else if (!strcmp(line, "abort")) {
    test_abort();
  } else if (!strcmp(line, "reset")) {
    on_reset(nullptr);
  } else if (sscanf(line, "board %d", &v) == 1 && v > 0) {
    g_board = v; say("board=%d", g_board);
  } else if (!strcmp(line, "help") || !strcmp(line, "?")) {
    say("commands: drv 0|1, ctrl 0|1, pulse, status, test, pass, fail, abort, reset, board N");
  } else {
    say("unknown: '%s' (help)", line);
  }
}

void poll_serial() {
  static char buf[64];
  static size_t n = 0;
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\n' || c == '\r') {
      buf[n] = 0;
      n = 0;
      handle_command(buf);
    } else if (n < sizeof(buf) - 1) {
      buf[n++] = c;
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);
#endif
  platform::log_init();
  core::logf("Apollo Link tester (fw %s) on %s\n", fw::kVersion, board::kName);

  // Outputs low BEFORE they become outputs: no blip on the shot line at boot.
  digitalWrite(kLinkDrive, LOW);
  pinMode(kLinkDrive, OUTPUT);
  if (kCornerDrive >= 0) {
    digitalWrite(kCornerDrive, board::kPaddleActiveHigh ? LOW : HIGH);
    pinMode(kCornerDrive, OUTPUT);
  }
  digitalWrite(kLinkCtrl, LOW);
  pinMode(kLinkCtrl, OUTPUT);
  gpio_set_drive_capability(static_cast<gpio_num_t>(kLinkCtrl), GPIO_DRIVE_CAP_3);
  pinMode(kLinkSense, INPUT_PULLUP);
  if (kCornerSense >= 0) pinMode(kCornerSense, INPUT_PULLUP);
  apply_drv(false);
  apply_ctrl(false);

  if (!g_display.begin()) {
    core::logf("ERROR: display init failed\n");
    return;
  }
  core::logf("Display up: %d x %d\n", g_display.width(), g_display.height());
  if (!g_touch.begin(g_display.width(), g_display.height()))
    core::logf("WARN: touch not detected\n");
  g_display.set_brightness(100);
  build_ui();
  read_rail();
  say("ready: ctrl=0 shot=0 pins drv=%d sense=%d ctrl=%d corner drv=%d sense=%d batnode=%.2fV",
      kLinkDrive, kLinkSense, kLinkCtrl, kCornerDrive, kCornerSense, g_rail_v);
  say("commands: drv 0|1, ctrl 0|1, pulse, status, test, pass, fail, abort, reset, board N");
}

void loop() {
  static uint32_t next_poll = 0, next_ui = 0, next_rail = 0;
  const uint32_t now = millis();
  if (static_cast<int32_t>(now - next_poll) >= 0) {
    next_poll = now + 5;
    poll_sense(g_link);
    poll_sense(g_corner);
  }
  if (g_pulse_until_ms && static_cast<int32_t>(now - g_pulse_until_ms) >= 0) {
    g_pulse_until_ms = 0;
    apply_drv(false);
    refresh_controls();
    say("pulse done");
  }
  if (static_cast<int32_t>(now - next_rail) >= 0) {
    next_rail = now + 500;
    read_rail();
  }
  if (static_cast<int32_t>(now - next_ui) >= 0) {
    next_ui = now + 100;
    refresh_sense_tile(g_link, g_link_state, g_link_sub);
    refresh_sense_tile(g_corner, g_corner_state, g_corner_sub);
    // The ADC reads the BATTERY node, not VCC_5V: the charger holds it near
    // 4.2 V whenever the P4's own USB-C feeds it, and it sits at ~0 V (no
    // cell) when the board is powered through the header instead — so this
    // doubles as a "which supply" indicator.
    char b[64];
    if (g_rail_v >= 2.5f) {
      snprintf(b, sizeof(b), "bat node %.2f V - USB power", g_rail_v);
    } else {
      snprintf(b, sizeof(b), "bat node %.2f V - header power (Link)", g_rail_v);
    }
    lv_label_set_text(g_rail_lbl, b);
    const uint32_t s = now / 1000;
    snprintf(b, sizeof(b), "up %lu:%02lu", static_cast<unsigned long>(s / 60), static_cast<unsigned long>(s % 60));
    lv_label_set_text(g_up_lbl, b);
    test_poll();
  }
  poll_serial();
  lv_timer_handler();
  delay(2);
}
