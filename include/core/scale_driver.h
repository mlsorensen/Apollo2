#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "core/ble.h"
#include "core/scale.h"

// Per-model scale protocol strategy. core::ScaleLink stays the generic
// connection/task manager; everything model-specific — which characteristic to
// subscribe, the post-subscribe init sequence, frame decoding, keepalives, and
// command bytes — lives behind IScaleDriver. Drivers are core-layer (no LVGL/
// Arduino/NimBLE/SDL) and hold per-connection state: ScaleLink creates a fresh
// instance for every connection attempt via make_scale_driver().

namespace core {

// Where a driver publishes decoded frames (implemented by ScaleLink). Called
// from the transport's notify thread; implementations lock internally.
// Publish weight LAST when a frame carries several fields — on_weight() bumps
// ScaleSnapshot.seq, and seq-locked consumers expect the trio to be consistent.
class IScaleSink {
 public:
  virtual ~IScaleSink() = default;
  virtual void on_weight(float grams) = 0;
  virtual void on_timer(uint32_t timer_ms) = 0;
  virtual void on_battery(int pct) = 0;
  // Readback of an on-scale setting (see ScaleSettingDesc): the option index
  // the scale reports for descriptor slot `index`, -1 for "present but not
  // one of ours" (e.g. an Umbra auto-off variant we don't offer).
  virtual void on_device_setting(int index, int option_idx) = 0;
};

class IScaleDriver {
 public:
  virtual ~IScaleDriver() = default;

  virtual const char* model() const = 0;  // for logs
  virtual ScaleFeatures features() const = 0;

  // Inspect the connected peripheral and return the notify characteristic to
  // subscribe (a driver family may span GATT generations with different
  // UUIDs), or nullptr if the device lacks the expected characteristics.
  virtual const char* select_notify(ble::ICentral& ble) = 0;

  // Post-subscribe init sequence (identify, notification request, ...).
  // Runs on the link thread; return false to abort the connection.
  virtual bool start(ble::ICentral& ble) = 0;

  // One notification payload (transport notify thread). Decode + publish.
  virtual void on_notify(const uint8_t* data, size_t len, IScaleSink& sink) = 0;

  // Called ~10x/s from the link thread while connected: heartbeats and stream
  // watchdogs. Return false to force a disconnect + reconnect.
  virtual bool tick(ble::ICentral& ble) = 0;

  virtual void tare(ble::ICentral& ble) = 0;

  // On-scale settings (ScaleSettingDesc). Defaults: none — only models whose
  // protocol covers them override. set_device_setting runs on the link
  // thread like tare(); readback arrives via IScaleSink::on_device_setting.
  virtual int device_setting_count() const { return 0; }
  virtual ScaleSettingDesc device_setting(int) const { return {}; }
  virtual void set_device_setting(ble::ICentral&, int /*index*/,
                                  int /*option_idx*/) {}
};

// Model classification from an advertised (or saved) device name. Nullptr-safe;
// make_scale_driver returns nullptr for unrecognized names.
bool scale_name_supported(const char* name);
std::shared_ptr<IScaleDriver> make_scale_driver(const char* name);

}  // namespace core
