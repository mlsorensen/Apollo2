#include "platform_esp32/umbra_partner.h"

#include <NimBLEDevice.h>
#include <Arduino.h>

#include <string>

#include "core/system.h"
#include "core/scale_driver.h"
#include "core/scale_link.h"

namespace platform {
namespace {

constexpr char kServiceUuid[] = "49535343-fe7d-4ae5-8fa9-9fafd205e455";
constexpr char kWriteUuid[] = "49535343-8841-43f4-a8d4-ecbe34729bb3";
constexpr char kNotifyUuid[] = "49535343-1e4d-4bd9-ba61-23c647249616";
constexpr uint8_t kInfo[] = {0xEF, 0xDD, 0x07, 0x07, 0x02, 0x19,
                             0x01, 0x00, 0x13, 0x02, 0x22, 0x16};
constexpr uint8_t kConfirm[] = {0xEF, 0xDD, 0x1B, 0x00, 0x00, 0x00};
constexpr uint8_t kEvent[] = {0xEF, 0xDD, 0x0C, 0x0C, 0x05, 0x02,
                              0x00, 0x00, 0x00, 0x02, 0x01, 0x07,
                              0x00, 0x00, 0x02, 0x17, 0x08};
constexpr uint8_t kKeepalive[] = {0xEF, 0xDD, 0x33, 0x04, 0x00, 0x00, 0x00,
                                  0x02, 0x08, 0x00, 0x00, 0x00, 0x06, 0x08};
constexpr uint8_t kSleepWire[] = {0, 7, 1, 2, 3};

void encode(uint8_t type, const uint8_t* payload, size_t n, uint8_t* out) {
  out[0] = 0xEF;
  out[1] = 0xDD;
  out[2] = type;
  uint8_t even = 0, odd = 0;
  for (size_t i = 0; i < n; ++i) {
    out[3 + i] = payload[i];
    if ((i & 1) == 0) even = static_cast<uint8_t>(even + payload[i]);
    else odd = static_cast<uint8_t>(odd + payload[i]);
  }
  out[3 + n] = even;
  out[4 + n] = odd;
}

bool checksum_ok(const uint8_t* frame, size_t n) {
  if (n < 5 || frame[0] != 0xEF || frame[1] != 0xDD) return false;
  uint8_t even = 0, odd = 0;
  for (size_t i = 3; i + 2 < n; ++i) {
    const size_t payload_i = i - 3;
    if ((payload_i & 1) == 0) even = static_cast<uint8_t>(even + frame[i]);
    else odd = static_cast<uint8_t>(odd + frame[i]);
  }
  return even == frame[n - 2] && odd == frame[n - 1];
}

int sleep_index(uint8_t wire) {
  for (int i = 0; i < 5; ++i) if (kSleepWire[i] == wire) return i;
  return -1;
}

}  // namespace

class UmbraPartner::ServerCallbacks final : public NimBLEServerCallbacks {
 public:
  explicit ServerCallbacks(UmbraPartner& owner) : owner_(owner) {}
  void onConnect(NimBLEServer*, NimBLEConnInfo& info) override {
    owner_.on_connect(info.getConnHandle());
  }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override {
    owner_.on_disconnect();
  }
 private:
  UmbraPartner& owner_;
};

class UmbraPartner::WriteCallbacks final : public NimBLECharacteristicCallbacks {
 public:
  explicit WriteCallbacks(UmbraPartner& owner) : owner_(owner) {}
  void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo&) override {
    const NimBLEAttValue value = characteristic->getValue();
    owner_.on_write(value.data(), value.length());
  }
 private:
  UmbraPartner& owner_;
};

class UmbraPartner::NotifyCallbacks final : public NimBLECharacteristicCallbacks {
 public:
  explicit NotifyCallbacks(UmbraPartner& owner) : owner_(owner) {}
  void onSubscribe(NimBLECharacteristic*, NimBLEConnInfo&,
                   uint16_t value) override {
    owner_.on_subscribe(value);
  }
 private:
  UmbraPartner& owner_;
};

bool UmbraPartner::begin() {
  if (initialized_.load()) return true;
  NimBLEDevice::setDeviceName(core::kPartnerAdvertName);
  NimBLEDevice::setSecurityAuth(false, false, false);
#if !defined(CONFIG_IDF_TARGET_ESP32P4)
  NimBLEDevice::setPower(9);
#endif
  server_ = NimBLEDevice::createServer();
  if (server_ == nullptr) return false;
  server_->setCallbacks(new ServerCallbacks(*this));
  NimBLEService* service = server_->createService(kServiceUuid);
  if (service == nullptr) return false;
  write_char_ = service->createCharacteristic(
      kWriteUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  notify_char_ = service->createCharacteristic(
      kNotifyUuid, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ);
  if (write_char_ == nullptr || notify_char_ == nullptr) return false;
  write_char_->setCallbacks(new WriteCallbacks(*this));
  notify_char_->setCallbacks(new NotifyCallbacks(*this));
  if (!server_->start()) return false;
  advertising_ = NimBLEDevice::getAdvertising();
  if (advertising_ == nullptr) return false;

  // Partner-mode pairing support for Umbra firmware 1.0.8.
  NimBLEAdvertisementData adv;
  adv.setFlags(0x06);
  adv.setName(core::kPartnerAdvertName);
  const char manufacturer[] = {0x42, 0x42, '0', '1'};
  adv.setManufacturerData(std::string(manufacturer, sizeof(manufacturer)));
  advertising_->setAdvertisementData(adv);
  advertising_->enableScanResponse(false);
  advertising_->setMinInterval(0x50);
  advertising_->setMaxInterval(0xA0);
  initialized_.store(true);
  scale_.set_partner_setting_writer([this](int index, int value) {
    pending_value_.store(value);
    pending_setting_.store(index);
  });
  core::logf("UmbraPartner: GATT peripheral ready\n");
  return true;
}

void UmbraPartner::set_enabled(bool enabled) {
  enabled_.store(enabled);
  if (!enabled) pending_setting_.store(-2);
}
void UmbraPartner::pause(bool paused) {
  paused_.store(paused);
  if (paused) pending_setting_.store(-2);
}

void UmbraPartner::poll() {
  if (!initialized_.load()) return;
  const bool wanted = enabled_.load() && !paused_.load() && !held_.load();
  if (wanted && !advertising_->isAdvertising() && !connected_.load())
    start_advertising();
  if (!wanted && !stopping_.exchange(true) &&
      (connected_.load() || advertising_->isAdvertising()))
    stop_and_sleep();
  if (wanted) stopping_.store(false);
  if (wanted) {
    // Handshake replies owed by the callbacks, in protocol order.
    if (owe_info_.exchange(false)) send_frame(kInfo, sizeof(kInfo));
    if (owe_confirm_.exchange(false)) send_frame(kConfirm, sizeof(kConfirm));
    if (owe_event_.exchange(false)) send_frame(kEvent, sizeof(kEvent));
    const int index = pending_setting_.exchange(-2);
    if (index != -2) send_setting(index, pending_value_.load());
    const uint32_t now = millis();
    if (connected_.load() && subscribed_.load() && !handshake_done_.load() &&
        now - last_info_ms_.load() >= 1200) {
      last_info_ms_.store(now);
      send_frame(kInfo, sizeof(kInfo));
    }
    if (connected_.load() && subscribed_.load() && session_active_.load() &&
        now - last_keepalive_ms_.load() >= 60) {
      last_keepalive_ms_.store(now);
      send_frame(kKeepalive, sizeof(kKeepalive));
    }
  }
}

void UmbraPartner::on_connect(uint16_t handle) {
  stopping_.store(false);
  conn_handle_.store(handle);
  connected_.store(true);
  subscribed_.store(false);
  handshake_done_.store(false);
  session_active_.store(false);
  owe_info_.store(false);
  owe_confirm_.store(false);
  owe_event_.store(false);
  last_info_ms_.store(millis());
  scale_.set_partner_connected(true);
  core::logf("UmbraPartner: Umbra connected\n");
}

void UmbraPartner::on_disconnect() {
  stopping_.store(false);
  connected_.store(false);
  subscribed_.store(false);
  handshake_done_.store(false);
  session_active_.store(false);
  owe_info_.store(false);
  owe_confirm_.store(false);
  owe_event_.store(false);
  conn_handle_.store(-1);
  scale_.set_partner_connected(false);
  core::logf("UmbraPartner: Umbra disconnected\n");
}

void UmbraPartner::on_subscribe(uint16_t value) {
  const bool on = (value & 1u) != 0;
  subscribed_.store(on);
  if (on) {
    last_info_ms_.store(millis());
    owe_info_.store(true);  // sent by poll()
  }
}

void UmbraPartner::on_write(const uint8_t* frame, size_t n) {
  if (frame == nullptr || n < 5 || frame[0] != 0xEF || frame[1] != 0xDD ||
      !checksum_ok(frame, n)) return;
  const uint8_t cmd = frame[2];
  if (cmd == 0x1A || cmd == 0x0B) {
    owe_confirm_.store(true);  // sent by poll()
    handshake_done_.store(true);
    scale_.on_partner_pairing_complete();
  } else if (cmd == 0x0C) {
    handshake_done_.store(true);
    session_active_.store(true);
    last_keepalive_ms_.store(millis());
    owe_event_.store(true);  // sent by poll()
  } else if (cmd == 0x1C && n >= 11) {
    const uint32_t raw = static_cast<uint32_t>(frame[3]) |
                         (static_cast<uint32_t>(frame[4]) << 8) |
                         (static_cast<uint32_t>(frame[5]) << 16) |
                         (static_cast<uint32_t>(frame[6]) << 24);
    const uint8_t dp = frame[7];
    constexpr float divisors[] = {1.0f, 10.0f, 100.0f, 1000.0f, 10000.0f};
    if (dp <= 4) {
      float grams = raw / divisors[dp];
      if (frame[8] & 0x02) grams = -grams;
      scale_.on_partner_weight(grams);
    }
  } else if (cmd == 0x08 && n >= 9 && frame[3] >= 5) {
    scale_.on_partner_battery(frame[4] & 0x7F);
    scale_.on_partner_setting(1, sleep_index(frame[5]));
    scale_.on_partner_setting(0, frame[6] <= 1 ? frame[6] : -1);
  }
}

void UmbraPartner::send_frame(const uint8_t* data, size_t len) {
  if (!connected_.load() || !subscribed_.load() || notify_char_ == nullptr) return;
  notify_char_->setValue(data, len);
  notify_char_->notify();
}

void UmbraPartner::send_setting(int index, int value) {
  if (!connected_.load() || !subscribed_.load()) return;
  if (index == -1) {
    const uint8_t payload[] = {0x00};
    uint8_t frame[6];
    encode(0x04, payload, sizeof(payload), frame);
    send_frame(frame, sizeof(frame));
    return;
  }
  uint8_t payload[3] = {0x00, 0, 0};
  if (index == 0) {
    payload[1] = 7;
    payload[2] = static_cast<uint8_t>(value);
  } else if (index == 1 && value >= 0 && value <= 4) {
    payload[1] = 6;
    payload[2] = kSleepWire[value];
  } else {
    return;
  }
  uint8_t frame[8];
  encode(0x0A, payload, sizeof(payload), frame);
  send_frame(frame, sizeof(frame));
}

void UmbraPartner::start_advertising() {
  if (advertising_ != nullptr && !advertising_->isAdvertising()) {
    if (advertising_->start())
      core::logf("UmbraPartner: advertising LUNAR-APOLLO\n");
    else
      core::logf("UmbraPartner: advertising start failed\n");
  }
}

void UmbraPartner::stop_and_sleep() {
  // Partner-mode support for Umbra firmware 1.0.8.
  if (connected_.load()) {
    if (subscribed_.load()) {
      const uint8_t sleep[] = {0xEF, 0xDD, 0x18, 0x08, 0x08, 0x00};
      send_frame(sleep, sizeof(sleep));
    }
    const int handle = conn_handle_.load();
    if (handle >= 0) {
      delay(40);
      server_->disconnect(static_cast<uint16_t>(handle));
    }
  }
  if (advertising_ != nullptr && advertising_->isAdvertising())
    advertising_->stop();
}

}  // namespace platform
