#include "core/scale_driver.h"

// Bookoo Themis Mini. Characteristics are discovered by UUID across all
// services (like MicraLink), so the parent service UUID doesn't matter.
// Notify = weight stream, write = commands. No init sequence, no keepalive.

namespace core {
namespace {

constexpr char kNotifyUuid[] = "ff11";
constexpr char kWriteUuid[] = "ff12";
constexpr uint8_t kTareCmd[] = {0x03, 0x0a, 0x01, 0x00, 0x00, 0x08};

// On-scale settings (see ScaleSettingDesc). Commands are the tare's 5-byte
// shape — 03 0a <op> 00 <value> — closed by the same XOR checksum; the scale
// echoes both current values in every weight notification, so readback is
// free and continuous.
constexpr uint8_t kOpBuzzer = 0x02;
constexpr uint8_t kOpAutoOff = 0x03;
// The wire accepts 0-5, but level 5 plays QUIETER than 4 (verified on HW;
// the vendor doc just says "00~05") — so only 0-4 are offered. A 5 set from
// the Bookoo app reads back as -1 ("--").
constexpr const char* kBuzzerLabels[] = {"Off", "1", "2", "3", "4"};
constexpr const char* kAutoOffLabels[] = {"5 min", "10 min", "15 min",
                                          "20 min", "30 min"};
constexpr uint8_t kAutoOffMinutes[] = {5, 10, 15, 20, 30};
constexpr ScaleSettingDesc kSettings[] = {
    {"Beep", kBuzzerLabels, 5},
    {"Auto-off", kAutoOffLabels, 5},
};
constexpr int kSettingCount = static_cast<int>(sizeof(kSettings) / sizeof(kSettings[0]));
enum { kSettingBeep = 0, kSettingAutoOff = 1 };

class BookooDriver : public IScaleDriver {
 public:
  const char* model() const override { return "Bookoo Themis"; }

  ScaleFeatures features() const override {
    return ScaleFeatures{.tare = true,
                         .flow = true,
                         .timer = true,
                         .battery = true,
                         .sleep = false};
  }

  const char* select_notify(ble::ICentral& ble) override {
    return ble.has_characteristic(kNotifyUuid) ? kNotifyUuid : nullptr;
  }

  bool start(ble::ICentral&) override { return true; }

  // Decode a 20-byte Bookoo Themis notification (per goscale themis/comms):
  //   [2..4] ms timer (24-bit BE); [6] sign ('-'=neg); [7..9] weight 24-bit BE
  //   /100; [13] battery %. (The UI derives flow rate from the weight stream,
  //   so we don't decode any flow field here.)
  void on_notify(const uint8_t* d, size_t len, IScaleSink& sink) override {
    if (d == nullptr || len < 20) return;
    const uint32_t ms =
        (static_cast<uint32_t>(d[2]) << 16) | (static_cast<uint32_t>(d[3]) << 8) | d[4];
    const int sign = (d[6] == 0x2D) ? -1 : 1;  // 0x2D == '-'
    const uint32_t raw_w =
        (static_cast<uint32_t>(d[7]) << 16) | (static_cast<uint32_t>(d[8]) << 8) | d[9];
    sink.on_timer(ms);
    sink.on_battery(d[13]);
    // On-scale settings ride along in the same frame: standby minutes in
    // [14..15] (big-endian, tenths of a minute), buzzer gear 0-5 in [16].
    const int gear = d[16];
    sink.on_device_setting(kSettingBeep, gear <= 4 ? gear : -1);
    const unsigned standby_min = ((static_cast<unsigned>(d[14]) << 8) | d[15]) / 10u;
    int off_idx = -1;
    for (int i = 0; i < 5; ++i) {
      if (standby_min == kAutoOffMinutes[i]) off_idx = i;
    }
    sink.on_device_setting(kSettingAutoOff, off_idx);
    sink.on_weight(sign * static_cast<float>(raw_w) / 100.0f);  // last: bumps seq
  }

  bool tick(ble::ICentral&) override { return true; }

  void tare(ble::ICentral& ble) override {
    ble.write(kWriteUuid, kTareCmd, sizeof(kTareCmd), /*with_response=*/true);
  }

  int device_setting_count() const override { return kSettingCount; }

  ScaleSettingDesc device_setting(int i) const override {
    return (i >= 0 && i < kSettingCount) ? kSettings[i] : ScaleSettingDesc{};
  }

  void set_device_setting(ble::ICentral& ble, int index, int option_idx) override {
    uint8_t op, value;
    if (index == kSettingBeep && option_idx >= 0 && option_idx <= 4) {
      op = kOpBuzzer;
      value = static_cast<uint8_t>(option_idx);
    } else if (index == kSettingAutoOff && option_idx >= 0 && option_idx < 5) {
      op = kOpAutoOff;
      value = kAutoOffMinutes[option_idx];
    } else {
      return;
    }
    uint8_t cmd[6] = {0x03, 0x0a, op, 0x00, value, 0x00};
    for (int i = 0; i < 5; ++i) cmd[5] ^= cmd[i];  // trailing XOR checksum
    ble.write(kWriteUuid, cmd, sizeof(cmd), /*with_response=*/true);
  }
};

}  // namespace

std::shared_ptr<IScaleDriver> make_bookoo_driver() {
  return std::make_shared<BookooDriver>();
}

}  // namespace core
