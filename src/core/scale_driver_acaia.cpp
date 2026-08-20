#include <atomic>
#include <cstdio>
#include <cstring>

#include "core/scale_driver.h"
#include "core/system.h"

// Acaia scale family — Umbra first, Lunar/Pyxis on the same protocol core.
// Byte-level reference: the acaia-umbra-protocol dossier, distilled from
// goscale pkg/scales/{umbra,lunar} + apollo's pyacaia (the code driving the
// user's machine today), cross-checked against upstream pyacaia and
// AcaiaArduinoBLE.
//
// All generations speak the same framing: EF DD | msgType | payload | ck1 ck2,
// checksum over the payload only (even-index byte sum -> ck1, odd -> ck2).
// Most incoming frames add a length byte: EF DD | cmd | len | content | ck1
// ck2, where len counts from itself through content (frame total = len + 5) —
// but only for the command ids marked kLenPrefixed below; the rest carry a
// fixed-size payload with no length byte at all.
//
// The generations differ in GATT + keepalive:
//  - Umbra: ST BlueNRG stock UUIDs (fe40 service), command char is
//    write-without-response only, NO keepalive of any kind — it pushes weight
//    continuously and a status frame every ~850 ms unprompted (that push is
//    where battery comes from; it sends no battery events at all). Deliberately
//    never polled: see b7892e5, extra traffic was implicated in the 62 s blip.
//    Has a sleep mode: a sleeping scale still advertises, and connecting wakes
//    it, so "sleeping" is a disconnected state, never a connected-but-idle one.
//  - New Lunar / Pyxis: 49535343-... service, write-with-response. The
//    keepalive here is a cmd-6 STATUS POLL every ~2 s, not the cmd-0 "alive"
//    heartbeat — that one belongs to the Pearl generation.
//  - Old Lunar / Pearl: single char 2a80 for both commands and weight; the
//    cmd-0 heartbeat is genuinely theirs.
//
// The handshake is REACTIVE on Lunar/Pyxis: the scale speaks first and we
// answer. It pushes cmd 7 (info) -> we identify; every cmd 8 (status) -> we
// re-arm the event registration. A registration write that is lost or lands
// before the scale's app layer is ready then self-corrects on the next status
// frame instead of wedging the connection into a permanent
// connected-but-no-weight state.
//
// Nothing at all is written for the first kInitialQuietMs after subscribing.
// That patience is copied deliberately: a handshake written on top of the CCCD
// write is what left one connect mute for six seconds. The only place we still
// diverge is the status-poll fallback in tick(), which re-sends the handshake
// while no weight has arrived — that covers a scale that never pushes cmd 7,
// which the reactive path alone cannot.
//
// The Umbra keeps the older proactive start(): it is the generation that
// already connects reliably, so it is the control, not a thing to retune.

namespace core {
namespace {

constexpr char kUmbraNotify[] = "0000fe42-8e22-4541-9d4c-21edae82ed19";
constexpr char kUmbraWrite[] = "0000fe41-8e22-4541-9d4c-21edae82ed19";
constexpr char kPyxisNotify[] = "49535343-1e4d-4bd9-ba61-23c647249616";
constexpr char kPyxisWrite[] = "49535343-8841-43f4-a8d4-ecbe34729bb3";
constexpr char kLegacyChar[] = "2a80";

constexpr uint8_t kHeader1 = 0xEF;
constexpr uint8_t kHeader2 = 0xDD;

// Outgoing message types.
constexpr uint8_t kMsgSystem = 0;     // cmd-0 "alive" heartbeat (Pearl-era)
constexpr uint8_t kMsgTare = 4;
constexpr uint8_t kMsgGetStatus = 6;  // status request (battery lives there)
constexpr uint8_t kMsgSetting = 10;   // settings write: 00, setting id, value
constexpr uint8_t kMsgIdentify = 11;
constexpr uint8_t kMsgEvent = 12;     // out: notification request; in: events

// On-scale settings (see ScaleSettingDesc), readable from the status frame
// and written with cmd 10. The two GATT generations use DIFFERENT setting-id
// spaces and different status-payload offsets (goscale umbra/ vs lunar/):
//   Umbra:       sleep id 6 (status payload[2]), beep id 7 (payload[3])
//   Lunar/Pyxis: sleep id 1 (status payload[4]), beep id 5 (payload[6])
constexpr uint8_t kUmbraSettingSleep = 6;
constexpr uint8_t kUmbraSettingBeep = 7;
constexpr uint8_t kUmbraSettingUnit = 8;
constexpr uint8_t kPyxisSettingUnit = 0;
constexpr uint8_t kPyxisSettingSleep = 1;
constexpr uint8_t kPyxisSettingBeep = 5;

// Unit wire values differ per generation: Umbra g=0 / oz=1 (status
// payload[4]); Lunar/Pyxis g=2 / oz=5 (status payload[2] & 0x7F).
constexpr uint8_t kUmbraUnitWire[] = {0, 1};
constexpr uint8_t kPyxisUnitWire[] = {2, 5};
constexpr float kOzToGrams = 28.3495f;

constexpr const char* kBeepLabels[] = {"Off", "On"};
// Umbra auto-off, sleep variants only, re-sorted for display (wire order is
// historical: 0=off, 1/2/3=sleep 5/10/30 m, 7=sleep 1 m). The wire also has
// power-off variants 4/5/6 — deliberately NOT offered: a powered-off scale
// stops advertising, which would turn the Home card's "Sleeping / connect to
// wake" promise into a lie. One set from elsewhere reads back as -1 ("--").
constexpr const char* kUmbraSleepLabels[] = {"Off", "1 min", "5 min",
                                             "10 min", "30 min"};
constexpr uint8_t kUmbraSleepWire[] = {0, 7, 1, 2, 3};
// Lunar/Pyxis auto-off: wire order is already the display order.
constexpr const char* kPyxisSleepLabels[] = {"Off", "5 min", "10 min",
                                             "20 min", "30 min", "60 min"};
constexpr const char* kUnitLabels[] = {"g", "oz"};
// Weighing mode, Lunar/Pyxis only (status payload[3] & 0x7F): reported but
// NOT settable over BLE — there is no mode write in the protocol, so the row
// is read-only. The Umbra status carries no mode byte at all.
constexpr const char* kModeLabels[] = {"1 Weighing",  "2 Dual display",
                                       "3 Pour over", "4 Espresso",
                                       "5 Espr + timer", "6 Auto-tare"};
constexpr ScaleSettingDesc kUmbraSettings[] = {
    {"Beep", kBeepLabels, 2},
    {"Auto sleep", kUmbraSleepLabels, 5},
    {"Unit", kUnitLabels, 2},
};
constexpr ScaleSettingDesc kPyxisSettings[] = {
    {"Beep", kBeepLabels, 2},
    {"Auto sleep", kPyxisSleepLabels, 6},
    {"Unit", kUnitLabels, 2},
    {"Mode", kModeLabels, 6, /*read_only=*/true},
};
enum { kSettingBeep = 0, kSettingSleep = 1, kSettingUnit = 2, kSettingMode = 3 };

// Incoming command ids + event subtypes (event msgType = notif-request id + 5:
// weight 0 -> 5, battery 1 -> 6, timer 2 -> 7).
constexpr uint8_t kCmdInfo = 7;
constexpr uint8_t kCmdStatus = 8;
constexpr uint8_t kEventWeight = 5;
constexpr uint8_t kEventBattery = 6;
constexpr uint8_t kEventTimer = 7;

// Payload length per command id. kLenPrefixed means the first payload byte is
// the length, counting itself; EVERY OTHER ENTRY IS A FIXED LENGTH WITH NO
// LENGTH BYTE. Assuming a length byte unconditionally mis-frames the rest of
// the stream the first time a fixed-length frame lands (cmds 28 / 51, the
// fastdata pair, are scale->app, so that is not hypothetical).
constexpr uint8_t kLenPrefixed = 255;
constexpr uint8_t kCmdLen[] = {
    255, 255, 1,  2, 1, 255, 1, 255, 255, 15, 3, 15, 255, 2, 255, 15, 255, 255,
    1,   1,   1,  1, 1, 1,   1, 1,   5,   1,  6, 1,  1,   1, 1,   1,  1,   7,
    3,   1,   1,  1, 1, 1,   1, 1,   1,   1,  1, 1,  1,   1, 1,   9,  7,   1};

// Payload length per event id inside a cmd-12 bundle. Ids 0-4 are the
// app->scale registration namespace; 5-11 are the scale->app events. Knowing
// the length of the ones we don't consume is what lets us skip them and keep
// parsing the rest of a bundle.
constexpr uint8_t kEventLen[] = {1, 1, 1, 0, 0, 6, 1, 3, 1, 2, 3, 2};
constexpr uint8_t kEventCount = sizeof(kEventLen);

constexpr uint32_t kHeartbeatMs = 2000;       // cmd-0 cadence (legacy only)
constexpr uint32_t kStatusPollMs = 2000;      // Lunar/Pyxis: keepalive cadence
constexpr uint32_t kLegacyStatusPollMs = 60000;  // legacy: battery refresh only
constexpr uint32_t kWeightStallMs = 5000;     // no WEIGHT -> re-handshake
constexpr uint32_t kWeightDeadMs = 30000;     // no WEIGHT -> drop the link
constexpr uint32_t kHandshakeMinGapMs = 2000;  // reactive-reply rate limit
constexpr uint32_t kInitialQuietMs = 3000;     // CCCD -> first command we send

// Build EF DD | type | payload | ck1 ck2. `out` needs n + 5 bytes.
size_t encode(uint8_t type, const uint8_t* payload, size_t n, uint8_t* out) {
  out[0] = kHeader1;
  out[1] = kHeader2;
  out[2] = type;
  uint32_t even = 0, odd = 0;
  for (size_t i = 0; i < n; ++i) {
    out[3 + i] = payload[i];
    ((i & 1) ? odd : even) += payload[i];
  }
  out[3 + n] = even & 0xFF;
  out[4 + n] = odd & 0xFF;
  return n + 5;
}

// Same sum, verifying direction. Frames that fail it are dropped: we used to
// accept anything that parsed, which let a mis-framed read publish garbage
// instead of re-syncing.
bool checksum_ok(const uint8_t* payload, size_t n, uint8_t ck1, uint8_t ck2) {
  uint32_t even = 0, odd = 0;
  for (size_t i = 0; i < n; ++i) ((i & 1) ? odd : even) += payload[i];
  return (even & 0xFF) == ck1 && (odd & 0xFF) == ck2;
}

class AcaiaDriver : public IScaleDriver {
 public:
  // umbra_hint: the advertised name said UMBRA. Only used to answer features()
  // truthfully before the first connect; once connected the GATT-detected
  // generation is authoritative.
  explicit AcaiaDriver(bool umbra_hint) : umbra_hint_(umbra_hint) {}

  const char* model() const override {
    switch (gen_) {
      case Gen::kUmbra: return "Acaia Umbra";
      case Gen::kPyxis: return "Acaia (Pyxis-gen)";
      case Gen::kLegacy: return "Acaia (legacy)";
      default: return "Acaia";
    }
  }

  ScaleFeatures features() const override {
    const bool umbra = gen_ == Gen::kUmbra || (gen_ == Gen::kUnknown && umbra_hint_);
    return ScaleFeatures{.tare = true,
                         .flow = true,
                         .timer = true,
                         .battery = true,
                         .sleep = umbra};
  }

  const char* select_notify(ble::ICentral& ble) override {
    if (ble.has_characteristic(kUmbraNotify)) {
      gen_ = Gen::kUmbra;
      return kUmbraNotify;
    }
    if (ble.has_characteristic(kPyxisNotify)) {
      gen_ = Gen::kPyxis;
      return kPyxisNotify;
    }
    if (ble.has_characteristic(kLegacyChar)) {
      gen_ = Gen::kLegacy;
      return kLegacyChar;
    }
    return nullptr;
  }

  bool start(ble::ICentral& ble) override {
    const uint32_t now = now_ms();
    last_rx_ms_.store(now);
    last_weight_ms_.store(now);  // grace period: the stream hasn't started yet
    next_beat_ms_ = now + kHeartbeatMs;
    next_ident_ms_ = now;
    next_register_ms_ = now;

    if (gen_ == Gen::kUmbra) {
      // Unchanged and proven: identify + register + one status read straight
      // after subscribe. The Umbra is the generation that already connects
      // reliably, so it does not get the Lunar's timing.
      next_status_ms_ = now + status_poll_ms();
      if (!send_handshake(ble)) return false;
      send_get_status(ble);
      return true;
    }

    // Lunar/Pyxis: SEND NOTHING YET. The Microchip-module generations want a
    // quiet period after the CCCD write — writing the handshake on top of it
    // is what produced the connect where the scale ignored everything and sat
    // mute for six seconds. The scale opens the conversation itself with a
    // cmd-7 info push (answered from tick), and our first outbound frame is
    // the status poll at kInitialQuietMs. Slower to first weight by design:
    // assume the timing is load-bearing rather than shave it.
    next_status_ms_ = now + kInitialQuietMs;
    return true;
  }

  void on_notify(const uint8_t* data, size_t len, IScaleSink& sink) override {
    if (data == nullptr || len == 0) return;
    last_rx_ms_.store(now_ms());
    // Frames can split across notifications (Umbra in practice sends one frame
    // per notify): accumulate + re-sync on the EF DD header, pyacaia-style.
    if (rx_len_ + len > sizeof(rx_)) rx_len_ = 0;  // overflow: start over
    std::memcpy(rx_ + rx_len_, data, len);
    rx_len_ += len;
    process_buffer(sink);
  }

  bool tick(ble::ICentral& ble) override {
    const uint32_t now = now_ms();
    // LIVENESS IS THE WEIGHT STREAM, NOT THE LINK. Timing any-notification
    // silence cannot see the failure that actually bites: the scale acks every
    // heartbeat and (on the Umbra) pushes status every ~850 ms, so a link whose
    // event registration never took looks perfectly healthy while no weight
    // ever arrives — and hangs there forever. Every generation streams weight
    // continuously while connected (a sleeping scale is a *disconnected* one),
    // so weight silence is unambiguous.
    const uint32_t weight_silent_ms = now - last_weight_ms_.load();

    // Answer whatever the scale asked for. Frames decode on the notify thread,
    // which has no transport handle, so on_notify() raises a flag and the send
    // happens here on the link thread. BOTH replies are rate-limited: a scale
    // that answers our identify with another info frame would otherwise
    // ping-pong at tick rate (one info per identify, forever).
    if (need_ident_.exchange(false) &&
        static_cast<int32_t>(now - next_ident_ms_) >= 0) {
      send_ident(ble);
      next_ident_ms_ = now + kHandshakeMinGapMs;
    }
    if (need_register_.exchange(false) &&
        static_cast<int32_t>(now - next_register_ms_) >= 0) {
      send_notif_request(ble);
      next_register_ms_ = now + kHandshakeMinGapMs;
    }

    if (weight_silent_ms > kWeightDeadMs) {
      logf("AcaiaDriver: no weight for %u ms (link %u ms) — reconnecting\n",
           static_cast<unsigned>(weight_silent_ms),
           static_cast<unsigned>(now - last_rx_ms_.load()));
      return false;
    }

    if (gen_ == Gen::kUmbra) {
      // Nothing to send: it streams weight unprompted and pushes its own status
      // frames. Deliberately never polled (b7892e5).
      return true;
    }

    if (gen_ == Gen::kLegacy && static_cast<int32_t>(now - next_beat_ms_) >= 0) {
      send_heartbeat(ble);  // the cmd-0 "alive" frame is this generation's
      next_beat_ms_ = now + kHeartbeatMs;
    }

    if (static_cast<int32_t>(now - next_status_ms_) >= 0) {
      // On Lunar/Pyxis this poll IS the keepalive, and each reply re-arms the
      // event registration (see handle_frame). Doubles as the battery refresh.
      //
      // Re-handshake on EVERY poll until the stream starts. A first handshake
      // the scale ignores is the common connect failure — observed on HW as a
      // completely silent link for ~6 s, recovered only once the stall window
      // expired. There is nothing to wait for in that state, so retry at the
      // poll cadence and cut time-to-first-weight to a couple of seconds.
      // After the stream exists, only a real stall re-handshakes.
      if (!weight_seen_.load()) {
        send_handshake(ble);
      } else if (weight_silent_ms > kWeightStallMs) {
        logf("AcaiaDriver: no weight for %u ms (link %u ms) — re-handshaking\n",
             static_cast<unsigned>(weight_silent_ms),
             static_cast<unsigned>(now - last_rx_ms_.load()));
        send_handshake(ble);
      }
      send_get_status(ble);
      next_status_ms_ = now + status_poll_ms();
    }
    return true;
  }

  void tare(ble::ICentral& ble) override {
    static constexpr uint8_t kZero[1] = {0};
    uint8_t buf[8];
    send(ble, buf, encode(kMsgTare, kZero, sizeof(kZero), buf));
  }

  // Legacy gen: no known settings protocol -> nothing exposed. Before the
  // first connect (Gen::kUnknown) the name hint picks the table, like
  // features() — the two generations' rows happen to line up anyway.
  int device_setting_count() const override {
    if (gen_ == Gen::kLegacy) return 0;
    return umbra_tables() ? 3 : 4;  // Umbra has no readable mode
  }

  ScaleSettingDesc device_setting(int i) const override {
    if (i < 0 || i >= device_setting_count()) return ScaleSettingDesc{};
    return umbra_tables() ? kUmbraSettings[i] : kPyxisSettings[i];
  }

  void set_device_setting(ble::ICentral& ble, int index, int option_idx) override {
    if (gen_ == Gen::kLegacy || gen_ == Gen::kUnknown) return;  // need the real gen
    const bool umbra = gen_ == Gen::kUmbra;
    uint8_t id, value;
    if (index == kSettingBeep && option_idx >= 0 && option_idx <= 1) {
      id = umbra ? kUmbraSettingBeep : kPyxisSettingBeep;
      value = static_cast<uint8_t>(option_idx);
    } else if (index == kSettingSleep && option_idx >= 0 &&
               option_idx < device_setting(kSettingSleep).option_count) {
      id = umbra ? kUmbraSettingSleep : kPyxisSettingSleep;
      value = umbra ? kUmbraSleepWire[option_idx]
                    : static_cast<uint8_t>(option_idx);
    } else if (index == kSettingUnit && option_idx >= 0 && option_idx <= 1) {
      id = umbra ? kUmbraSettingUnit : kPyxisSettingUnit;
      value = umbra ? kUmbraUnitWire[option_idx] : kPyxisUnitWire[option_idx];
    } else {
      return;
    }
    const uint8_t payload[3] = {0x00, id, value};
    uint8_t buf[8];
    send(ble, buf, encode(kMsgSetting, payload, sizeof(payload), buf));
  }

 private:
  enum class Gen { kUnknown, kUmbra, kPyxis, kLegacy };

  bool umbra_tables() const {
    return gen_ == Gen::kUmbra || (gen_ == Gen::kUnknown && umbra_hint_);
  }

  const char* write_uuid() const {
    switch (gen_) {
      case Gen::kUmbra: return kUmbraWrite;
      case Gen::kPyxis: return kPyxisWrite;
      default: return kLegacyChar;
    }
  }

  bool send(ble::ICentral& ble, const uint8_t* frame, size_t len) {
    // Umbra's command char is write-without-response only; Pyxis-gen expects
    // write-with-response (the transport downgrades if unsupported).
    return ble.write(write_uuid(), frame, len, /*with_response=*/gen_ == Gen::kPyxis);
  }

  bool send_handshake(ble::ICentral& ble) {
    return send_ident(ble) && send_notif_request(ble);
  }

  bool send_ident(ble::ICentral& ble) {
    // The standard Acaia-client appid.
    static constexpr uint8_t kIdent[15] = {'0', '1', '2', '3', '4', '5', '6', '7',
                                           '8', '9', '0', '1', '2', '3', '4'};
    uint8_t buf[24];
    return send(ble, buf, encode(kMsgIdentify, kIdent, sizeof(kIdent), buf));
  }

  bool send_notif_request(ble::ICentral& ble) {
    // Event registration: leading length, then entries. An entry is an event id
    // plus an interval byte ONLY for the ids that take one — weight/1,
    // battery/2, timer/5 do, key(3) and setting(4) do NOT. We used to append a
    // 0 interval to those two and declare length 11; the scale re-reads the
    // stray zeros as another weight entry with interval 4 plus a truncated
    // tail, so the registration it ends up with is not the one we asked for.
    // On the wire: EF DD 0C 09 00 01 01 02 02 05 03 04 15 06.
    static constexpr uint8_t kNotifRequest[] = {9, 0, 1, 1, 2, 2, 5, 3, 4};
    uint8_t buf[24];
    return send(ble, buf, encode(kMsgEvent, kNotifRequest, sizeof(kNotifRequest), buf));
  }

  uint32_t status_poll_ms() const {
    return gen_ == Gen::kPyxis ? kStatusPollMs : kLegacyStatusPollMs;
  }

  void send_heartbeat(ble::ICentral& ble) {
    static constexpr uint8_t kBeat[2] = {2, 0};
    uint8_t buf[8];
    send(ble, buf, encode(kMsgSystem, kBeat, sizeof(kBeat), buf));
  }

  void send_get_status(ble::ICentral& ble) {
    static constexpr uint8_t kZero[1] = {0};
    uint8_t buf[8];
    send(ble, buf, encode(kMsgGetStatus, kZero, sizeof(kZero), buf));
  }

  // Diagnostics for the every-60s bogus-weight mystery (~-10 g blips seen on
  // the Umbra): dump raw bytes for anything unusual — implausible weights,
  // frame types we don't recognize, and re-sync skips. Rate-limited to 1/s so
  // a storm can't swamp serial. Remove once root-caused.
  void log_frame(const char* why, const uint8_t* d, size_t n) {
    const uint32_t now = now_ms();
    if (now - last_diag_ms_ < 1000) return;
    last_diag_ms_ = now;
    char hex[3 * 24 + 1];
    const size_t m = n < 24 ? n : 24;
    for (size_t i = 0; i < m; ++i) std::snprintf(hex + 3 * i, 4, "%02X ", d[i]);
    logf("AcaiaDriver: %s [%u]: %s\n", why, static_cast<unsigned>(n), hex);
  }

  void process_buffer(IScaleSink& sink) {
    size_t pos = 0;
    for (;;) {
      const size_t scan_start = pos;
      while (pos + 1 < rx_len_ && !(rx_[pos] == kHeader1 && rx_[pos + 1] == kHeader2)) {
        ++pos;  // re-sync to the next header
      }
      if (pos != scan_start)
        log_frame("resync skipped", rx_ + scan_start, pos - scan_start);
      if (pos + 3 > rx_len_) break;  // need header + cmd

      const uint8_t cmd = rx_[pos + 2];
      if (cmd >= sizeof(kCmdLen)) {
        pos += 2;  // not a command id at all: skip this header, re-sync
        continue;
      }
      size_t payload_len = kCmdLen[cmd];
      if (payload_len == kLenPrefixed) {
        if (pos + 4 > rx_len_) break;  // need the length byte
        payload_len = rx_[pos + 3];    // counts itself
        if (payload_len < 1) {
          pos += 2;
          continue;
        }
      }
      const size_t total = payload_len + 5;
      if (total > sizeof(rx_)) {
        pos += 2;
        continue;
      }
      if (pos + total > rx_len_) break;  // incomplete: wait for more bytes

      const uint8_t* payload = rx_ + pos + 3;
      if (!checksum_ok(payload, payload_len, payload[payload_len],
                       payload[payload_len + 1])) {
        log_frame("bad checksum", rx_ + pos, total);
        pos += 2;  // don't consume: the length may be garbage too, re-sync
        continue;
      }
      handle_frame(cmd, payload, payload_len, sink);
      pos += total;
    }
    if (pos > 0) {  // keep the unconsumed tail
      std::memmove(rx_, rx_ + pos, rx_len_ - pos);
      rx_len_ -= pos;
    }
  }

  // payload = everything between the cmd byte and ck1. For a length-prefixed
  // command payload[0] is that length; for a fixed-length one the content
  // starts at payload[0]. Every command we consume is length-prefixed.
  void handle_frame(uint8_t cmd, const uint8_t* payload, size_t n,
                    IScaleSink& sink) {
    if (cmd == kCmdInfo) {
      // Scale-info push. This is the cue to identify — the scale sends it once
      // it is ready to talk, so answering it gets the handshake ordering right
      // however long the scale takes to come up.
      if (n >= 6) {
        logf("AcaiaDriver: scale fw %u.%u.%u\n", static_cast<unsigned>(payload[3]),
             static_cast<unsigned>(payload[4]), static_cast<unsigned>(payload[5]));
      }
      need_ident_.store(true);
      return;
    }

    if (cmd == kCmdStatus) {
      // Battery is b1 of the status blob on every generation (Lunar keeps a
      // timer-running flag in bit 7; the Umbra value is plain 0-100 < 128, so
      // the mask is harmless there). The Umbra pushes one of these ~every
      // 850 ms on its own — battery needs no polling or event registration.
      if (n >= 2) sink.on_battery(payload[1] & 0x7F);
      // On-scale settings ride in the same blob, at generation-specific
      // offsets (see the setting-id table up top).
      if (gen_ == Gen::kUmbra && n >= 5) {
        sink.on_device_setting(kSettingBeep, payload[3] <= 1 ? payload[3] : -1);
        int sleep_idx = -1;
        for (int i = 0; i < 5; ++i) {
          if (payload[2] == kUmbraSleepWire[i]) sleep_idx = i;
        }
        sink.on_device_setting(kSettingSleep, sleep_idx);
        const int unit_idx =
            payload[4] == kUmbraUnitWire[0] ? 0 : payload[4] == kUmbraUnitWire[1] ? 1 : -1;
        unit_oz_.store(unit_idx == 1);
        sink.on_device_setting(kSettingUnit, unit_idx);
      } else if (gen_ == Gen::kPyxis && n >= 7) {
        sink.on_device_setting(kSettingBeep, payload[6] <= 1 ? payload[6] : -1);
        sink.on_device_setting(kSettingSleep, payload[4] <= 5 ? payload[4] : -1);
        const uint8_t u = payload[2] & 0x7F;
        const int unit_idx = u == kPyxisUnitWire[0] ? 0 : u == kPyxisUnitWire[1] ? 1 : -1;
        unit_oz_.store(unit_idx == 1);
        sink.on_device_setting(kSettingUnit, unit_idx);
        const uint8_t mode = payload[3] & 0x7F;
        sink.on_device_setting(kSettingMode, mode <= 5 ? mode : -1);
      }
      // Re-arm the event registration off the back of the status reply. On
      // Lunar/Pyxis that is every ~2 s for the life of the link, because our
      // own poll sets the cadence. The Umbra pushes status every ~850 ms
      // unprompted, so there we stop once weight is flowing rather than add a
      // steady stream of writes to a generation that already works.
      if (gen_ != Gen::kUmbra || !weight_seen_.load()) need_register_.store(true);
      return;
    }

    if (cmd != kMsgEvent || n < 1) {
      log_frame("unknown cmd", payload, n);
      return;
    }

    // A cmd-12 frame can BUNDLE several events back to back: id byte + a fixed
    // payload whose size comes from kEventLen. Skipping the ones we don't
    // consume by their real length (rather than bailing out of the bundle)
    // matters because we register setting events, so they DO arrive — and a
    // bundle that leads with one used to take the weight behind it down too.
    size_t off = 1;  // payload[0] is the length byte
    while (off < n) {
      const uint8_t event = payload[off];
      if (event < kEventWeight || event >= kEventCount) {
        log_frame("unknown event", payload, n);
        break;
      }
      const size_t elen = kEventLen[event];
      if (off + 1 + elen > n) {
        log_frame("short event", payload, n);
        break;
      }
      const uint8_t* p = payload + off + 1;
      switch (event) {
        case kEventWeight: {
          // Stream liveness (see tick) is marked on RECEIPT, before the
          // excursion hold can swallow the value — a held frame still proves
          // the scale is streaming.
          last_weight_ms_.store(now_ms());
          weight_seen_.store(true);
          // The stream is in the scale's DISPLAY unit — an oz-mode scale
          // sends ounces (the weight event has no unit field; the status
          // frame's unit is the authority). Convert so everything downstream
          // (targets, detector, graphs) stays gram-space regardless of what
          // the scale's own screen shows.
          float w = decode_weight(p);
          if (unit_oz_.load()) w *= kOzToGrams;
          if (w <= 5500.0f && w >= -5500.0f && accept_weight(w)) sink.on_weight(w);
          break;
        }
        case kEventBattery:
          sink.on_battery(p[0] & 0x7F);
          break;
        case kEventTimer:
          sink.on_timer((p[0] * 60u + p[1]) * 1000u + p[2] * 100u);  // min, sec, tenths
          break;
        default:
          break;  // key / setting / countdown / ack: skip, keep parsing
      }
      off += 1 + elen;
    }
  }

  // Internal-measurement excursion hold. Every ~62s the Umbra interrupts its
  // weight stream for ~800 ms with values from some internal measurement —
  // purpose unknown (zero maintenance? temperature? battery-under-load?);
  // what's established is the SIGNATURE: a settling ramp of small NEGATIVE
  // values unrelated to the pan load (captured: -3.1 -3.1 -3.9 -4.0 -4.1...,
  // flags walking unstable->stable; other sessions -8.3, -10.2) terminated
  // by an exact 0.0, after which normal readings resume. Detector:
  // when the stream steps from the published baseline into the offset band
  // (< -0.5 g, > -20 g, deviating > 2.5 g), HOLD — publish nothing and wait.
  // The cycle always returns to baseline (via its 0.0 when the pan is empty),
  // so the whole excursion is discarded with zero artifacts; if the stream
  // does NOT return within ~1.2 s it was a real weight change (e.g. a small
  // tared item removed) and publishing resumes with that value. Outside the
  // band-plus-step trigger nothing is ever delayed: dosing, shots, cup
  // handling, and big negatives (cup off a tared scale, < -20 g) publish
  // immediately.
  bool accept_weight(float w) {
    const uint32_t now = now_ms();
    if (!holding_) {
      if (w < -0.5f && w > -20.0f && baseline_g_ - w > 2.5f) {
        holding_ = true;
        hold_since_ms_ = now;
        logf("AcaiaDriver: [%u] auto-zero hold (w=%.1f baseline=%.1f)\n",
             static_cast<unsigned>(now), static_cast<double>(w),
             static_cast<double>(baseline_g_));
        return false;
      }
      baseline_g_ = w;
      return true;
    }
    const float dev = w > baseline_g_ ? w - baseline_g_ : baseline_g_ - w;
    if (dev <= 2.0f) {
      holding_ = false;  // returned to baseline: the excursion never happened
      baseline_g_ = w;
      return true;
    }
    // Any frame OUTSIDE the offset band means physical interaction (a hand or
    // scoop in the load path spikes well past it) — the auto-zero cycle never
    // leaves the band. Release immediately so real removals show in ~a frame
    // or two instead of waiting out the timeout.
    if (w < -20.0f || w > baseline_g_ + 2.5f) {
      holding_ = false;
      baseline_g_ = w;
      return true;
    }
    if (now - hold_since_ms_ > kHoldTimeoutMs) {
      holding_ = false;  // no return, no contact spike: a real change — adopt it
      logf("AcaiaDriver: [%u] auto-zero hold released (real change w=%.1f)\n",
           static_cast<unsigned>(now), static_cast<double>(w));
      baseline_g_ = w;
      return true;
    }
    return false;  // mid-excursion: swallow
  }

  // 6-byte weight payload: bytes 0-3 raw u32 — endianness varies by model
  // and even within one scale's streams (the user's Umbra streams LE despite
  // earlier lore saying BE). Parse BE first, fall back to LE when implausible
  // (> 2000 g) — goscale/apollo's trick, one parser covers everything seen.
  // byte 4 = decimal places, byte 5 bit1 = negative.
  static float decode_weight(const uint8_t* p) {
    const uint32_t be = (static_cast<uint32_t>(p[0]) << 24) |
                        (static_cast<uint32_t>(p[1]) << 16) |
                        (static_cast<uint32_t>(p[2]) << 8) | p[3];
    const uint32_t le = (static_cast<uint32_t>(p[3]) << 24) |
                        (static_cast<uint32_t>(p[2]) << 16) |
                        (static_cast<uint32_t>(p[1]) << 8) | p[0];
    // Decimals byte: 1..4 -> /10../10000, and 0 ALSO means /100 (0 and 2 are
    // the same divisor — treating 0 as /1 reads 100x high). Anything above 4 is
    // out of spec; clamp rather than publishing the 0.0 g a stricter decoder
    // would, since a phantom zero is worse than a stale-looking weight here.
    const uint8_t dp = p[4] == 0 ? 2 : p[4];
    float div = 1.0f;
    for (uint8_t i = 0; i < dp && i < 4; ++i) div *= 10.0f;
    float w = static_cast<float>(be) / div;
    if (w > 2000.0f) w = static_cast<float>(le) / div;
    if (p[5] & 0x02) w = -w;
    return w;
  }

  Gen gen_ = Gen::kUnknown;
  const bool umbra_hint_;

  // BLE notify thread only.
  uint8_t rx_[256];
  size_t rx_len_ = 0;
  uint32_t last_diag_ms_ = 0;  // log_frame rate limit
  // Auto-zero excursion hold state (BLE thread only; see accept_weight).
  static constexpr uint32_t kHoldTimeoutMs = 1000;  // bursts measure <= ~850ms
  float baseline_g_ = 0.0f;
  bool holding_ = false;
  uint32_t hold_since_ms_ = 0;

  // Written on the notify thread, read from tick().
  std::atomic<uint32_t> last_rx_ms_{0};      // any frame — diagnostics only
  std::atomic<uint32_t> last_weight_ms_{0};  // weight events: the real liveness
  std::atomic<bool> weight_seen_{false};
  std::atomic<bool> unit_oz_{false};  // status says the display unit is oz
  std::atomic<bool> need_ident_{false};     // cmd 7 arrived -> identify
  std::atomic<bool> need_register_{false};  // cmd 8 arrived -> re-register

  // Link thread only.
  uint32_t next_beat_ms_ = 0;
  uint32_t next_status_ms_ = 0;
  uint32_t next_ident_ms_ = 0;
  uint32_t next_register_ms_ = 0;
};

}  // namespace

std::shared_ptr<IScaleDriver> make_acaia_driver(bool umbra_hint) {
  return std::make_shared<AcaiaDriver>(umbra_hint);
}

}  // namespace core
