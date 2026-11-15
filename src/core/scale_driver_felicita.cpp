#include <atomic>
#include <cstdlib>
#include <cstring>

#include "core/scale_driver.h"
#include "core/system.h"

// Felicita (Arc; the Incline/Parallel share the protocol). One characteristic
// FFE1 (service FFE0) carries both directions: subscribe and the scale streams
// unprompted, commands are single bytes written to the same UUID. No init
// sequence, no keepalive. Protocol per Beanconqueror (felicitaScale.ts),
// pyfelicita and Acaia_Felicita_ArduinoBLE, which agree on the frame:
//
//   18 bytes: [2] sign ('+'/'-')  [3..8] six ASCII digits, centi-units
//             [9..10] unit (" g" on an Arc; "oz")  [15] battery (raw, not percent)
//
// e.g. an Arc at zero: 01 02 2b 30 30 30 30 30 30 20 67 43 e3 4f 22 8c 0d 0a
// ([11..14] vary and are undocumented; [16..17] are CR LF).
//
// The frame carries no timer and no settings readback, so the driver exposes
// weight + battery + tare only. The scale's own beep is a blind toggle (0x42)
// with nothing to read back — it doesn't fit ScaleSettingDesc, so it isn't
// offered. The on-device modes (auto-timer/auto-tare, 0x31..0x35) are left
// alone on purpose: the scale owns its settings; the manual recommends
// weight-only mode for brew-by-weight.

namespace core {
namespace {

constexpr char kUuid[] = "ffe1";  // notify + write
constexpr uint8_t kCmdTare = 0x54;
constexpr size_t kFrameLen = 18;

// Battery byte -> percent. The references disagree (Beanconqueror 129..158,
// pyfelicita 137..161), and neither matches the scale's own app: on an Arc,
// raw 155 is 100 % and raw 140 is 20 % in the Felicita app (HW, 2026-11-15),
// and the line through those two puts empty at ~136. The raw value is logged
// on change, for calibrating other Felicita models.
constexpr int kBatteryRawMin = 136;
constexpr int kBatteryRawMax = 155;

// The scale streams continuously while on (pyfelicita treats 5 s of silence
// as disconnected), so a silent link is a dead one.
constexpr uint32_t kStreamDeadMs = 5000;

class FelicitaDriver : public IScaleDriver {
 public:
  const char* model() const override { return "Felicita"; }

  ScaleFeatures features() const override {
    return ScaleFeatures{.tare = true,
                         .flow = true,    // derived from the weight stream
                         .timer = false,  // not in the frame
                         .battery = true,
                         .battery_low_pct = 15,
                         .sleep = false};
  }

  const char* select_notify(ble::ICentral& ble) override {
    return ble.has_characteristic(kUuid) ? kUuid : nullptr;
  }

  bool start(ble::ICentral&) override {
    last_rx_ms_.store(now_ms());
    return true;
  }

  void on_notify(const uint8_t* d, size_t len, IScaleSink& sink) override {
    if (d == nullptr || len != kFrameLen) return;
    last_rx_ms_.store(now_ms());
    if (!first_frame_ready_.load()) {  // one raw dump per connection (tick logs)
      std::memcpy(first_frame_, d, kFrameLen);
      first_frame_ready_.store(true);
    }

    uint32_t raw = 0;
    for (size_t i = 3; i <= 8; ++i) {
      if (d[i] < '0' || d[i] > '9') return;  // malformed
      raw = raw * 10 + (d[i] - '0');
    }

    // Hysteresis like pyfelicita: the byte jitters by one at rest (140/141
    // flips every second on an Arc), so only a move of 2+ is taken — and
    // logged, or the log fills with the jitter.
    const int batt = d[15];
    if (std::abs(batt - battery_shown_) >= 2 || battery_shown_ < 0) {
      battery_shown_ = batt;
      battery_raw_.store(batt);
    }
    int pct = (battery_shown_ - kBatteryRawMin) * 100 / (kBatteryRawMax - kBatteryRawMin);
    sink.on_battery(pct < 0 ? 0 : pct > 100 ? 100 : pct);

    // Apollo works in grams only; an ounce frame would read 28x too light.
    // The unit's padding side isn't consistent across sources (the Arc sends
    // " g", the references assume "g "), so look for the 'o' of "oz".
    if (d[9] == 'o' || d[10] == 'o') {
      ounces_.store(true);
      return;
    }
    const int sign = (d[2] == '-') ? -1 : 1;
    sink.on_weight(sign * static_cast<float>(raw) / 100.0f);  // last: bumps seq
  }

  uint32_t first_weight_ms() const override { return 0; }  // own stream timeout below
  bool tick(ble::ICentral&) override {
    // Logging lives here (link thread), not in the notify callback.
    if (first_frame_ready_.load() && !first_frame_logged_) {
      first_frame_logged_ = true;
      char hex[kFrameLen * 3 + 1];
      for (size_t i = 0; i < kFrameLen; ++i) {
        static constexpr char kHex[] = "0123456789abcdef";
        hex[i * 3] = kHex[first_frame_[i] >> 4];
        hex[i * 3 + 1] = kHex[first_frame_[i] & 0x0F];
        hex[i * 3 + 2] = ' ';
      }
      hex[kFrameLen * 3 - 1] = '\0';
      logf("FelicitaDriver: first frame %s\n", hex);
    }
    const int batt = battery_raw_.load();
    if (batt >= 0 && batt != battery_logged_) {
      battery_logged_ = batt;
      logf("FelicitaDriver: battery raw %d\n", batt);
    }
    if (ounces_.load() && !ounces_logged_) {
      ounces_logged_ = true;
      logf("FelicitaDriver: scale is in ounces — switch it to grams\n");
    }
    if (now_ms() - last_rx_ms_.load() > kStreamDeadMs) {
      logf("FelicitaDriver: stream silent — reconnecting\n");
      return false;
    }
    return true;
  }

  void tare(ble::ICentral& ble) override {
    ble.write(kUuid, &kCmdTare, 1, /*with_response=*/true);
  }

 private:
  std::atomic<uint32_t> last_rx_ms_{0};
  // Notify-thread -> link-thread handoff for the diagnostic log lines.
  uint8_t first_frame_[kFrameLen] = {};
  std::atomic<bool> first_frame_ready_{false};
  std::atomic<int> battery_raw_{-1};
  std::atomic<bool> ounces_{false};
  int battery_shown_ = -1;  // notify thread only
  // Link thread only.
  bool first_frame_logged_ = false;
  int battery_logged_ = -1;
  bool ounces_logged_ = false;
};

}  // namespace

std::shared_ptr<IScaleDriver> make_felicita_driver() {
  return std::make_shared<FelicitaDriver>();
}

}  // namespace core
