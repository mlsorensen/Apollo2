#include "core/scale_driver.h"

// Bookoo Themis Mini. Characteristics are discovered by UUID across all
// services (like MicraLink), so the parent service UUID doesn't matter.
// Notify = weight stream, write = commands. No init sequence, no keepalive.

namespace core {
namespace {

constexpr char kNotifyUuid[] = "ff11";
constexpr char kWriteUuid[] = "ff12";
constexpr uint8_t kTareCmd[] = {0x03, 0x0a, 0x01, 0x00, 0x00, 0x08};

class BookooDriver : public IScaleDriver {
 public:
  const char* model() const override { return "Bookoo Themis"; }

  ScaleFeatures features() const override {
    return ScaleFeatures{.tare = true,
                         .flow = true,
                         .timer = true,
                         .battery = true,
                         .beep = false,
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
    sink.on_weight(sign * static_cast<float>(raw_w) / 100.0f);  // last: bumps seq
  }

  bool tick(ble::ICentral&) override { return true; }

  void tare(ble::ICentral& ble) override {
    ble.write(kWriteUuid, kTareCmd, sizeof(kTareCmd), /*with_response=*/true);
  }
};

}  // namespace

std::shared_ptr<IScaleDriver> make_bookoo_driver() {
  return std::make_shared<BookooDriver>();
}

}  // namespace core
