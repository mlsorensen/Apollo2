#include <atomic>
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
// Incoming frames add a length byte: EF DD | cmd | len | content | ck1 ck2,
// where len counts from itself through content (frame total = len + 5).
//
// The generations differ in GATT + keepalive:
//  - Umbra: ST BlueNRG stock UUIDs (fe40 service), command char is
//    write-without-response only, NO heartbeat (goscale omits it entirely) —
//    liveness is the notify stream + a ~30 s silence watchdog. Has a sleep
//    mode: a sleeping scale still advertises and auto-wakes on connect.
//  - New Lunar / Pyxis: 49535343-... service, write-with-response, STRICT
//    heartbeat — miss it and the scale drops the link (pyacaia's main battle).
//  - Old Lunar: single char 2a80 for both commands and weight.

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
constexpr uint8_t kMsgSystem = 0;     // heartbeat
constexpr uint8_t kMsgTare = 4;
constexpr uint8_t kMsgGetStatus = 6;  // status request (battery lives there)
constexpr uint8_t kMsgIdentify = 11;
constexpr uint8_t kMsgEvent = 12;     // out: notification request; in: events

// Incoming command ids + event subtypes (event msgType = notif-request id + 5:
// weight 0 -> 5, battery 1 -> 6, timer 2 -> 7).
constexpr uint8_t kCmdStatus = 8;
constexpr uint8_t kEventWeight = 5;
constexpr uint8_t kEventBattery = 6;
constexpr uint8_t kEventTimer = 7;

constexpr uint32_t kHeartbeatMs = 2000;      // apollo's cadence (Lunar/Pyxis)
constexpr uint32_t kStallMs = 5000;          // silence -> re-identify (goscale)
constexpr uint32_t kUmbraSilenceMs = 30000;  // Umbra watchdog -> reconnect
constexpr uint32_t kStatusPollMs = 60000;    // battery refresh

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
    // Beep IS settable on the Umbra (encode(10,[0,7,v])) — off until the UI
    // grows a control for it.
    const bool umbra = gen_ == Gen::kUmbra || (gen_ == Gen::kUnknown && umbra_hint_);
    return ScaleFeatures{.tare = true,
                         .flow = true,
                         .timer = true,
                         .battery = true,
                         .beep = false,
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
    next_beat_ms_ = now + kHeartbeatMs;
    next_status_ms_ = now + kStatusPollMs;
    return send_ident(ble);
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
    const uint32_t silent_ms = now - last_rx_ms_.load();

    if (gen_ == Gen::kUmbra) {
      // No heartbeat AND no polling — goscale sends NOTHING to the Umbra after
      // init, and neither do we. A 60 s get-status poll here made the scale
      // blip a bogus constant weight (~-10 g for ~0.5 s, every poll, verified
      // on HW); battery arrives as pushed kEventBattery events instead.
      // Liveness is the notify stream itself.
      if (silent_ms > kUmbraSilenceMs) {
        logf("AcaiaDriver: %u ms of silence — reconnecting\n",
             static_cast<unsigned>(silent_ms));
        return false;
      }
      return true;
    }

    if (static_cast<int32_t>(now - next_beat_ms_) >= 0) {
      // Mandatory keepalive. Apollo's stall recovery: heartbeats acked but no
      // data flowing -> re-identify instead of waiting for a dead link.
      if (silent_ms > kStallMs) send_ident(ble);
      send_heartbeat(ble);
      next_beat_ms_ = now + kHeartbeatMs;
    }
    if (static_cast<int32_t>(now - next_status_ms_) >= 0) {
      send_get_status(ble);  // battery refresh (goscale-lunar polls this too)
      next_status_ms_ = now + kStatusPollMs;
    }
    return true;
  }

  void tare(ble::ICentral& ble) override {
    static constexpr uint8_t kZero[1] = {0};
    uint8_t buf[8];
    send(ble, buf, encode(kMsgTare, kZero, sizeof(kZero), buf));
  }

 private:
  enum class Gen { kUnknown, kUmbra, kPyxis, kLegacy };

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

  bool send_ident(ble::ICentral& ble) {
    // Pyxis-style identify (used by goscale for Umbra + new Lunar), then the
    // notification request: weight, battery, timer-every-5, key, setting.
    static constexpr uint8_t kIdent[15] = {'0', '1', '2', '3', '4', '5', '6', '7',
                                           '8', '9', '0', '1', '2', '3', '4'};
    static constexpr uint8_t kNotifRequest[] = {9, 0, 1, 1, 2, 2, 5, 3, 4};
    uint8_t buf[24];
    if (!send(ble, buf, encode(kMsgIdentify, kIdent, sizeof(kIdent), buf))) return false;
    return send(ble, buf, encode(kMsgEvent, kNotifRequest, sizeof(kNotifRequest), buf));
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

  void process_buffer(IScaleSink& sink) {
    size_t pos = 0;
    for (;;) {
      while (pos + 1 < rx_len_ && !(rx_[pos] == kHeader1 && rx_[pos + 1] == kHeader2)) {
        ++pos;  // re-sync to the next header
      }
      if (pos + 4 > rx_len_) break;  // need header + cmd + len
      const size_t total = static_cast<size_t>(rx_[pos + 3]) + 5;
      if (total < 6 || total > sizeof(rx_)) {
        pos += 2;  // implausible length: skip this header, re-sync
        continue;
      }
      if (pos + total > rx_len_) break;  // incomplete: wait for more bytes
      handle_frame(rx_ + pos, total, sink);
      pos += total;
    }
    if (pos > 0) {  // keep the unconsumed tail
      std::memmove(rx_, rx_ + pos, rx_len_ - pos);
      rx_len_ -= pos;
    }
  }

  // frame = EF DD cmd len content... ck1 ck2 (checksums not verified — neither
  // reference implementation does).
  void handle_frame(const uint8_t* f, size_t total, IScaleSink& sink) {
    const uint8_t cmd = f[2];
    const size_t content_len = total - 6;  // after the len byte, before ck1

    if (cmd == kCmdStatus) {
      // Battery is b1 of the status blob on every generation (Lunar keeps a
      // timer-running flag in bit 7; the Umbra value is plain 0-100 < 128, so
      // the mask is harmless there).
      if (content_len >= 1) sink.on_battery(f[4] & 0x7F);
      return;
    }

    if (cmd != kMsgEvent || content_len < 1) return;
    const uint8_t event = f[4];
    const uint8_t* p = f + 5;
    const size_t n = content_len - 1;

    if (event == kEventWeight && n >= 6) {
      sink.on_weight(decode_weight(p));
    } else if (event == kEventBattery && n >= 1) {
      sink.on_battery(p[0] & 0x7F);  // pushed by the scale (notif request id 1)
    } else if (event == kEventTimer && n >= 3) {
      sink.on_timer((p[0] * 60u + p[1]) * 1000u + p[2] * 100u);  // min, sec, tenths
    }
    // Button (8) and heartbeat-response (11) events: nothing to publish.
  }

  // 6-byte weight payload: bytes 0-3 raw u32 — BIG-endian on Umbra, LITTLE on
  // Lunar. Parse BE first, fall back to LE when implausible (> 2000 g) —
  // goscale/apollo's trick, one parser covers both. byte 4 = decimal places,
  // byte 5 bit1 = negative.
  static float decode_weight(const uint8_t* p) {
    const uint32_t be = (static_cast<uint32_t>(p[0]) << 24) |
                        (static_cast<uint32_t>(p[1]) << 16) |
                        (static_cast<uint32_t>(p[2]) << 8) | p[3];
    const uint32_t le = (static_cast<uint32_t>(p[3]) << 24) |
                        (static_cast<uint32_t>(p[2]) << 16) |
                        (static_cast<uint32_t>(p[1]) << 8) | p[0];
    float div = 1.0f;
    for (uint8_t i = 0; i < p[4] && i < 4; ++i) div *= 10.0f;
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

  std::atomic<uint32_t> last_rx_ms_{0};  // written on notify, read from tick()

  // Link thread only.
  uint32_t next_beat_ms_ = 0;
  uint32_t next_status_ms_ = 0;
};

}  // namespace

std::shared_ptr<IScaleDriver> make_acaia_driver(bool umbra_hint) {
  return std::make_shared<AcaiaDriver>(umbra_hint);
}

}  // namespace core
