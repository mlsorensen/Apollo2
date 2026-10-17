#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

class NimBLEServer;
class NimBLECharacteristic;
class NimBLEAdvertising;

namespace core { class ScaleLink; }

namespace platform {

// Partner-mode pairing support for Umbra firmware 1.0.8.
class UmbraPartner {
 public:
  explicit UmbraPartner(core::ScaleLink& scale) : scale_(scale) {}

  bool begin();
  bool ready() const { return initialized_.load(); }  // registered this boot
  void set_enabled(bool enabled);
  void pause(bool paused);
  // Pairing-prompt hold: no advertising while set, so the user can pick
  // "Use unpaired" before a ready Umbra grabs the partner link.
  void hold(bool held) { held_.store(held); }
  void poll();

 private:
  class ServerCallbacks;
  class WriteCallbacks;
  class NotifyCallbacks;
  friend class ServerCallbacks;
  friend class WriteCallbacks;
  friend class NotifyCallbacks;

  void on_connect(uint16_t handle);
  void on_disconnect();
  void on_write(const uint8_t* data, size_t len);
  void on_subscribe(uint16_t value);
  void send_frame(const uint8_t* data, size_t len);
  void send_setting(int index, int value);
  void start_advertising();
  void stop_and_sleep();

  core::ScaleLink& scale_;
  NimBLEServer* server_ = nullptr;
  NimBLECharacteristic* write_char_ = nullptr;
  NimBLECharacteristic* notify_char_ = nullptr;
  NimBLEAdvertising* advertising_ = nullptr;
  std::atomic<bool> initialized_{false};
  std::atomic<bool> enabled_{false};
  std::atomic<bool> paused_{false};
  std::atomic<bool> held_{false};
  std::atomic<bool> connected_{false};
  std::atomic<bool> subscribed_{false};
  std::atomic<bool> stopping_{false};
  std::atomic<bool> handshake_done_{false};
  std::atomic<bool> session_active_{false};
  // Frames owed by the NimBLE callbacks. ONLY poll() (loop()) sends: a send is
  // setValue()+notify() on one characteristic, and two threads interleaving
  // those can put one frame's bytes under the other's notify.
  std::atomic<bool> owe_info_{false};
  std::atomic<bool> owe_confirm_{false};
  std::atomic<bool> owe_event_{false};
  std::atomic<int> pending_setting_{-2};  // -2 = no command; -1 = tare
  std::atomic<int> pending_value_{-1};
  std::atomic<int> conn_handle_{-1};
  std::atomic<uint32_t> last_info_ms_{0};
  std::atomic<uint32_t> last_keepalive_ms_{0};
};

}  // namespace platform
