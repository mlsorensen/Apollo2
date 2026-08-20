#include <atomic>

#include "core/scale_driver.h"
#include "core/system.h"

// Varia Aku ("Varia AKU-MICRO", module EC324-BT). Subscribe FFF1 and it
// streams unprompted; commands go to FFF2 as write-without-response. No init
// sequence, no keepalive. Framing (GATT-probed on HW, fw 1.6.05): every
// frame is FA <op> <len> <payload...> <xor of op..payload>. Report ops seen:
// 0x01 weight (3-byte payload, ~9 Hz), 0x85 battery percent (1 byte, ~4 s
// cadence), 0x86 unknown (1 byte, once at connect). Command op: 0x82 tare.
// No settings traffic observed — nothing to expose.

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
                         .battery = true,  // 0x85 report frames
                         .sleep = false};
  }

  const char* select_notify(ble::ICentral& ble) override {
    return ble.has_characteristic(kNotifyUuid) ? kNotifyUuid : nullptr;
  }

  bool start(ble::ICentral&) override {
    last_rx_ms_.store(now_ms());
    return true;
  }

  // FA <op> <len> <payload...> <xor>: weight op 0x01 (sign in bit 4 of the
  // first payload byte, magnitude 20-bit big-endian centigrams), battery
  // op 0x85 (percent).
  void on_notify(const uint8_t* d, size_t len, IScaleSink& sink) override {
    if (d == nullptr || len < 5 || d[0] != 0xFA) return;
    last_rx_ms_.store(now_ms());
    const size_t total = static_cast<size_t>(d[2]) + 4;  // hdr+op+len+..+ck
    if (len < total) return;
    uint8_t ck = 0;
    for (size_t i = 1; i < total - 1; ++i) ck ^= d[i];
    if (ck != d[total - 1]) return;
    if (d[1] == 0x85 && d[2] >= 1) {
      sink.on_battery(d[3] <= 100 ? d[3] : 100);
      return;
    }
    if (d[1] != 0x01 || d[2] < 3) return;
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
