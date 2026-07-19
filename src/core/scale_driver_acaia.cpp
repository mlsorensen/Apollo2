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
    if (!send_ident(ble)) return false;
    // One status request up front (reference clients poll status right after
    // connecting): guarantees an early battery reading without waiting for a
    // pushed battery event. Polls are harmless — the 60s housekeeping frame
    // (see accept_weight) arrives with or without them.
    send_get_status(ble);
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
    const uint32_t silent_ms = now - last_rx_ms_.load();

    if (gen_ == Gen::kUmbra) {
      // No keepalive: the cmd-0 heartbeat is a Pearl-era requirement — Umbra
      // clients (goscale included) send nothing after init and the scale
      // holds the link fine. Battery arrives as pushed kEventBattery events
      // + the one start() status read. Liveness is the notify stream itself.
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
    // Identify (the standard Acaia-client appid), then the notification
    // request: len 11, then (event, interval) pairs weight/1, battery/2,
    // timer/5, key/0, setting/0 — the canonical registration set. (pyacaia's
    // shorter 4-pair variant also works.)
    static constexpr uint8_t kIdent[15] = {'0', '1', '2', '3', '4', '5', '6', '7',
                                           '8', '9', '0', '1', '2', '3', '4'};
    static constexpr uint8_t kNotifRequest[] = {11, 0, 1, 1, 2, 2, 5, 3, 0, 4, 0};
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

    if (cmd != kMsgEvent || content_len < 1) {
      log_frame("unknown cmd", f, total);
      return;
    }
    const uint8_t event = f[4];
    const uint8_t* p = f + 5;
    const size_t n = content_len - 1;

    if (event == kEventWeight && n >= 6) {
      const float w = decode_weight(p);
      // Burst diagnostic (bounded, NOT rate-limited — the earlier 1/s limit
      // hid that the 60s anomaly is a ~0.5s BURST of frames, so a
      // first-frame latch can't kill it): log every suspicious frame with
      // the decode + the gate's verdict, until the budget runs out.
      const bool suspicious =
          w < -5.0f || w - prev_weight_g_ > 3.0f || prev_weight_g_ - w > 3.0f;
      const bool ok = accept_weight(w);
      if (suspicious && anomaly_dump_left_ > 0) {
        --anomaly_dump_left_;
        const uint32_t saved = last_diag_ms_;
        last_diag_ms_ = 0;
        char tag[48];
        std::snprintf(tag, sizeof(tag), "w=%.1f %s", static_cast<double>(w),
                      ok ? "PUB" : "DROP");
        log_frame(tag, f, total);
        last_diag_ms_ = saved;
      }
      if (ok) sink.on_weight(w);
    } else if (event == kEventBattery && n >= 1) {
      sink.on_battery(p[0] & 0x7F);  // pushed by the scale (notif request id 1)
    } else if (event == kEventTimer && n >= 3) {
      sink.on_timer((p[0] * 60u + p[1]) * 1000u + p[2] * 100u);  // min, sec, tenths
    } else if (event != 8 && event != 11) {
      // Button (8) and heartbeat-response (11): known, nothing to publish.
      log_frame("unknown event", f, total);
    }
  }

  // Weight publish gate — VALUE heuristics, matching how Acaia's own
  // software behaves, because on the Umbra the frame FLAGS can't help: its
  // 60s zero-tracking housekeeping frame (~-10 g, e.g.
  // EF DD 0C 08 05 53 00 00 00 01 0F 5C 14) carries the same type bits as
  // the whole normal stream (normal frame captured on HW:
  // ... D6 0D 00 00 01 0C ...). The gate:
  //  - drop out-of-range readings (|w| > 5500 g);
  //  - drop the FIRST frame below -5 g (first-negative latch): a lone
  //    housekeeping frame between normal readings never shows, while a real
  //    sustained negative (cup lifted off a tared scale) publishes from its
  //    second frame on;
  //  - drop one-frame transients to (near-)zero right after a non-zero
  //    reading.
  bool accept_weight(float w) {
    bool drop = false;
    if (w > 5500.0f || w < -5500.0f) drop = true;
    const bool near_zero = w >= -1.0f && w <= 1.0f;
    const bool prev_near_zero = prev_weight_g_ >= -1.0f && prev_weight_g_ <= 1.0f;
    if (!prev_near_zero && near_zero) drop = true;
    if (prev_weight_g_ != 0.0f && w == 0.0f) drop = true;
    if (w < -5.0f) {
      if (!minus5_latch_) {
        minus5_latch_ = true;
        drop = true;
      }
    } else {
      minus5_latch_ = false;
    }
    prev_weight_g_ = w;  // tracked whether or not this frame published
    return !drop;
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
  uint32_t last_diag_ms_ = 0;  // log_frame rate limit
  float prev_weight_g_ = 0.0f;  // accept_weight state
  bool minus5_latch_ = false;   // first-negative latch (see accept_weight)
  uint8_t anomaly_dump_left_ = 40;  // burst diagnostic budget (see on weight)

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
