#pragma once

#include <cstdint>
#include <ctime>

// Battery estimate for a scale whose own gauge FREEZES after it sleeps. Pure
// domain logic: no clock, no storage, no transport — the caller feeds it
// readings, session ends and the Unix time, and persists state() when
// take_dirty() says so.
//
// Why (Acaia Umbra, owner, 2026-10-30): once the Umbra has slept its battery
// percent stops updating until the scale restarts (a power cycle, or plugging /
// unplugging the charger). Every client sees the same stale value, so the
// gauge itself stops. The pack then dies "suddenly" at an unchanged reading.
//
// Rules, settled with the owner:
//   - LIVE or STALE, nothing else. A live reading is shown as reported, charging
//     or not — no charge detection, no distrust of plugged-in values.
//   - The scale goes STALE when we send it to sleep (a session that ends with
//     the sleep command). It goes LIVE again on the first reading that differs
//     from the stored last-live value: a frozen gauge cannot change, so a
//     changed value proves the scale restarted. The sleep timer is NOT
//     consulted — with the timer Off the scale stays up, its value moves and
//     the first changed reading flips it back to live.
//   - Stale estimate = last-live % - rate (%/day) x days since going stale,
//     floored at 0. It feeds the normal low-battery warning, marked estimated.
//   - Nothing is persisted per reading (a live gauge flickers 35/34/35): only
//     the transitions and session ends dirty the state.
//   - Calibration, armed by the user ("charge fully, use until empty, then
//     charge"): measures the next whole stale period. Start = the last-live %
//     when it goes stale; end = the stale -> live flip (the charge restart),
//     assuming the scale hit 0 % when it was last seen. The result is offered
//     to the user (Store / Discard); off-procedure runs are theirs to discard.
//     Arming is the UI's to gate (disabled while the scale's sleep is Off).
//   - No trusted clock (now == 0): never go stale, and a stale estimate holds
//     at its last-live value rather than guessing.

namespace core {

constexpr int kScaleDrainDefaultPctPerDay = 15;  // stock Umbra: ~6-7 days a charge
constexpr int kScaleDrainMinPctPerDay = 1;
constexpr int kScaleDrainMaxPctPerDay = 30;

// Everything that must survive a reboot. The caller stores it field by field.
struct ScaleBatteryState {
  bool have_live = false;      // a reading has ever been seen
  uint8_t live_pct = 0;        // the last live reading
  uint32_t stale_since = 0;    // Unix s when it went stale; 0 = live
  uint32_t last_seen = 0;      // Unix s the scale was last seen (session end)
  bool cal_armed = false;      // the user armed a calibration
  bool cal_running = false;    // armed AND a stale period began after arming
  uint8_t cal_start_pct = 0;   // last-live % when that stale period began
};

struct ScaleBatteryView {
  bool valid = false;
  int pct = 0;
  bool estimated = false;
};

class ScaleBatteryEstimate {
 public:
  void load(const ScaleBatteryState& s) { s_ = s; }
  const ScaleBatteryState& state() const { return s_; }
  // True once after anything worth persisting changed.
  bool take_dirty() {
    const bool d = dirty_;
    dirty_ = false;
    return d;
  }

  // Every battery reading the scale reports.
  void on_reading(int pct);
  // A session with the scale ended; `slept` = we sent it the sleep command.
  void on_session_end(bool slept, std::time_t now);

  // What to show now, given the rate setting (%/day).
  ScaleBatteryView view(int rate_pct_per_day, std::time_t now) const;

  // Calibration. arm() restarts a run; disarm() cancels one.
  void arm();
  void disarm();
  bool armed() const { return s_.cal_armed; }
  // A finished run's measured rate in %/day (once), or -1. Clamped to the
  // setting's range; the caller offers it to the user.
  int take_calibration_result() {
    const int r = cal_result_;
    cal_result_ = -1;
    return r;
  }

 private:
  ScaleBatteryState s_;
  bool dirty_ = false;
  int cal_result_ = -1;
};

}  // namespace core
