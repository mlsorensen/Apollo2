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
        .name = lunar_ ? "LUNAR-A1B2" : umbra_ ? "UMBRA-7F3A" : "BOOKOO_THEMIS",
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
  int device_setting_count() const override {
    return lunar_ ? 4 : umbra_ ? 3 : 2;
  }
  core::ScaleSettingDesc device_setting(int i) const override {
    static constexpr const char* kGear[] = {"Off", "1", "2", "3", "4"};
    static constexpr const char* kOnOff[] = {"Off", "On"};
    static constexpr const char* kBookooOff[] = {"5 min", "10 min", "15 min",
                                                 "20 min", "30 min"};
    static constexpr const char* kUmbraSleep[] = {"Off", "1 min", "5 min",
                                                  "10 min", "30 min"};
    static constexpr const char* kLunarSleep[] = {"Off",    "5 min",  "10 min",
                                                  "20 min", "30 min", "60 min"};
    static constexpr const char* kModes[] = {"1 Weighing",  "2 Dual display",
                                             "3 Pour over", "4 Espresso",
                                             "5 Espr + timer", "6 Auto-tare"};
    if (i == 0)
      return (umbra_ || lunar_) ? core::ScaleSettingDesc{"Beep", kOnOff, 2}
                                : core::ScaleSettingDesc{"Beep", kGear, 5};
    if (i == 1)
      return lunar_ ? core::ScaleSettingDesc{"Auto sleep", kLunarSleep, 6}
             : umbra_ ? core::ScaleSettingDesc{"Auto sleep", kUmbraSleep, 5}
                      : core::ScaleSettingDesc{"Auto-off", kBookooOff, 5};
    if (i == 2 && (umbra_ || lunar_)) {
      static constexpr const char* kUnits[] = {"g", "oz"};
      return core::ScaleSettingDesc{"Unit", kUnits, 2};
    }
    if (i == 3 && lunar_)
      return core::ScaleSettingDesc{"Mode", kModes, 6, /*read_only=*/true};
    return core::ScaleSettingDesc{};
  }
  int device_setting_value(int i) const override {
    return (i >= 0 && i < 4) ? setting_value_[i] : -1;
  }
  void set_device_setting(int i, int v) override {
    if (i >= 0 && i < 4 && v >= 0 && v < device_setting(i).option_count)
      setting_value_[i] = v;
  }

  void set_connected(bool c) { connected_ = c; }
  // Umbra persona: sleep-capable scale, for rendering the "Sleeping" Home state.
  void set_umbra(bool u) { umbra_ = u; }
  // Lunar persona: Acaia with a Unit row + the read-only weighing-mode row.
  void set_lunar(bool l) {
    lunar_ = l;
    setting_value_[0] = l ? 1 : 3;  // Beep: On (0/1 list) vs gear 3 (0-4 list)
  }

 private:
  bool connected_ = true;
  bool umbra_ = false;
  bool lunar_ = false;
  int setting_value_[4] = {3, 2, 0, 3};  // Beep / Auto-off / Unit g / Mode 4
};

}  // namespace host
