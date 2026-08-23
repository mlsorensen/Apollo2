#pragma once

#include "core/machine.h"

// "Machine is up to temperature" latch behind the ready chime. Pure domain
// logic over MachineSnapshot — like every core header it stays free of
// LVGL/Arduino/NimBLE/SDL, and it owns no audio: the caller plays the sound.

namespace core {

// A warm-up is only worth announcing once, so the latch has to tell a real
// heat-up from the small sags the boilers make holding temperature:
//   - it arms only after a sustained, plausible deficit (a stale 0 C right
//     after connecting is not a warm-up, and neither is a 1 C dip);
//   - once fired it stays latched for the rest of the session: at most ONE
//     chime per turn-on. Only leaving the running state (standby, off at the
//     switch — which drops the BLE link — or disconnect) clears the latch, so
//     the next turn-on's warm-up chimes again. A temperature-drop re-arm used
//     to exist here (heavy steaming / a refill cools a boiler 10 C+) and made
//     nuisance re-chimes; removed 2026-08-23.
// "Ready" itself is just !heating, so it inherits derive_heating()'s
// hysteresis and its steam rule: with the steam boiler off the brew boiler
// alone decides, with it on both boilers must be there.
// Deliberately above kHeatingOnDeltaC: a genuine warm-up is tens of degrees, so
// requiring more than the bare "heating" threshold costs nothing and keeps a
// boiler hovering at the edge of the band from arming the chime.
constexpr float kChimeArmDeltaC = 6.0f;    // deficit that counts as warming up
constexpr int kChimeArmPolls = 3;          // consecutive polls before arming

class ReadyChime {
 public:
  // Feed one refresh, with the heating flag the caller already derived.
  // Returns true on the single poll where the chime should sound.
  bool update(const MachineSnapshot& s, bool heating) {
    // Not running: forget everything, so the next warm-up starts clean.
    if (s.link != Link::Connected || s.power != Power::On) {
      reset();
      steam_known_ = false;
      return false;
    }
    // Steam toggled: the ready condition itself changed, so restart the
    // arming observation — but NOT the fired latch: once-per-turn-on is a
    // hard cap, and toggling steam mid-session must not buy a second chime.
    if (steam_known_ && s.steam_enabled != steam_was_) {
      armed_ = false;
      arming_ = 0;
    }
    steam_was_ = s.steam_enabled;
    steam_known_ = true;

    const bool warming =
        heating && (deficit(s.brew_temp_c, s.brew_target_c) >= kChimeArmDeltaC ||
                    (s.steam_enabled &&
                     deficit(s.boiler_temp_c, s.boiler_target_c) >= kChimeArmDeltaC));
    if (warming) {
      if (arming_ < kChimeArmPolls) ++arming_;
      if (arming_ >= kChimeArmPolls) armed_ = true;
    } else {
      arming_ = 0;
    }

    if (heating || !armed_ || fired_) return false;
    armed_ = false;
    fired_ = true;
    return true;
  }

  void reset() {
    armed_ = false;
    fired_ = false;
    arming_ = 0;
  }

 private:
  // How far a boiler is below its set point. A missing reading (either side
  // still 0 in the gap between connecting and the first poll) is no deficit —
  // otherwise the first real reading would look like a completed warm-up.
  static float deficit(float temp_c, float target_c) {
    if (temp_c <= 0.0f || target_c <= 0.0f) return 0.0f;
    return target_c - temp_c;
  }

  bool armed_ = false;       // a warm-up has been observed; ready will chime
  bool fired_ = false;       // chimed this session; only reset() clears it
  int arming_ = 0;           // consecutive warming polls seen so far
  bool steam_was_ = false;   // last steam_enabled, for toggle detection
  bool steam_known_ = false;
};

}  // namespace core
