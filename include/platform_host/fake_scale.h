#pragma once

#include "core/scale.h"

// Host stand-in for a Bluetooth scale. On the device this role is played by the
// NimBLE ScaleLink; here it returns canned state so the scale-aware UI can be
// designed and rendered on a laptop. Setters let the simulator render specific
// states (connected vs not).

namespace host {

class FakeScale : public core::IScale {
 public:
  core::ScaleSnapshot snapshot() const override {
    return core::ScaleSnapshot{
        .name = umbra_ ? "UMBRA-7F3A" : "BOOKOO_THEMIS",
        .connected = connected_,
        .weight_g = 36.4f,
        .timer_ms = 27000,
        .battery_valid = true,
        .battery_pct = 78,
        .seq = 1,
    };
  }
  core::ScaleFeatures features() const override {
    return core::ScaleFeatures{.tare = true,
                               .flow = true,
                               .timer = true,
                               .battery = true,
                               .sleep = umbra_};
  }
  void tare() override {}

  // On-scale settings, canned per persona: the Bookoo exposes its 0-5 buzzer
  // gear, the Umbra a Beep on/off — both with an auto-off timer.
  int device_setting_count() const override { return 2; }
  core::ScaleSettingDesc device_setting(int i) const override {
    static constexpr const char* kGear[] = {"Off", "1", "2", "3", "4"};
    static constexpr const char* kOnOff[] = {"Off", "On"};
    static constexpr const char* kBookooOff[] = {"5 min", "10 min", "15 min",
                                                 "20 min", "30 min"};
    static constexpr const char* kUmbraSleep[] = {"Off", "1 min", "5 min",
                                                  "10 min", "30 min"};
    if (i == 0)
      return umbra_ ? core::ScaleSettingDesc{"Beep", kOnOff, 2}
                    : core::ScaleSettingDesc{"Beep", kGear, 5};
    if (i == 1)
      return umbra_ ? core::ScaleSettingDesc{"Auto sleep", kUmbraSleep, 5}
                    : core::ScaleSettingDesc{"Auto-off", kBookooOff, 5};
    return core::ScaleSettingDesc{};
  }
  int device_setting_value(int i) const override {
    return (i >= 0 && i < 2) ? setting_value_[i] : -1;
  }
  void set_device_setting(int i, int v) override {
    if (i >= 0 && i < 2 && v >= 0 && v < device_setting(i).option_count)
      setting_value_[i] = v;
  }

  void set_connected(bool c) { connected_ = c; }
  // Umbra persona: sleep-capable scale, for rendering the "Sleeping" Home state.
  void set_umbra(bool u) { umbra_ = u; }

 private:
  bool connected_ = true;
  bool umbra_ = false;
  int setting_value_[2] = {3, 2};  // Beep gear 3 / Auto-off 15 min (Bookoo)
};

}  // namespace host
