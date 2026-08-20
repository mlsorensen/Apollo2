#include <atomic>

#include "core/scale_driver.h"
#include "core/system.h"

// Varia Aku (per goscale aku/comms). The simplest protocol we support:
// subscribe FFF1 and weight streams continuously; commands go to FFF2 as
// write-without-response. No init sequence, no keepalive, and the frames
// carry nothing but weight — no timer, battery, or settings.

namespace core {
namespace {

constexpr char kNotifyUuid[] = "fff1";
constexpr char kWriteUuid[] = "fff2";

// The scale streams while healthy, so a silent link is a dead one (goscale
// cuts at 1 s; we allow more slack before forcing the reconnect path).
constexpr uint32_t kStreamDeadMs = 5000;

class AkuDriver : public IScaleDriver {
 public:
  const char* model() const override { return "Varia Aku"; }

  ScaleFeatures features() const override {
    return ScaleFeatures{.tare = true,
                         .flow = true,  // derived from the weight stream
                         .timer = false,
                         .battery = false,
                         .sleep = false};
  }

  const char* select_notify(ble::ICentral& ble) override {
    return ble.has_characteristic(kNotifyUuid) ? kNotifyUuid : nullptr;
  }

  bool start(ble::ICentral&) override {
    last_rx_ms_.store(now_ms());
    return true;
  }

  // Weight-only frame: [1] 0x01 marks a weight update; sign in bit 4 of [3],
  // magnitude 20-bit big-endian across [3..5] in centigrams.
  void on_notify(const uint8_t* d, size_t len, IScaleSink& sink) override {
    if (d == nullptr || len < 6) return;
    last_rx_ms_.store(now_ms());
    if (d[1] != 0x01) return;
    const int sign = (d[3] & 0x10) ? -1 : 1;
    const uint32_t raw = (static_cast<uint32_t>(d[3] & 0x0F) << 16) |
                         (static_cast<uint32_t>(d[4]) << 8) | d[5];
    sink.on_weight(sign * static_cast<float>(raw) / 100.0f);
  }

  bool tick(ble::ICentral&) override {
    if (now_ms() - last_rx_ms_.load() > kStreamDeadMs) {
      logf("AkuDriver: stream silent — reconnecting\n");
      return false;
    }
    return true;
  }

  // FA 82 01 01 + XOR of the three bytes after the header.
  void tare(ble::ICentral& ble) override {
    static constexpr uint8_t kTare[] = {0xFA, 0x82, 0x01, 0x01,
                                        0x82 ^ 0x01 ^ 0x01};
    ble.write(kWriteUuid, kTare, sizeof(kTare), /*with_response=*/false);
  }

 private:
  std::atomic<uint32_t> last_rx_ms_{0};
};

}  // namespace

std::shared_ptr<IScaleDriver> make_aku_driver() {
  return std::make_shared<AkuDriver>();
}

}  // namespace core
