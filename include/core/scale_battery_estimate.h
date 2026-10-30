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
//     A run that can't be measured (charged before a whole period was seen)
//     is still reported — as 0, "too short" — so the user is told, never
//     silently disarmed. A pending result is part of the persisted state.
//   - No trusted clock (now == 0 — the caller passes 0 until a real sync, not
//     a seeded guess): never go stale, and a stale estimate holds at its
//     last-live value rather than guessing.
//   - The persisted blob carries a tag of the scale's MAC: one written for a
//     different scale (an older build, rolled back to, can leave it behind
//     when the slot is re-saved) is ignored on load.

namespace core {

constexpr int kScaleDrainDefaultPctPerDay = 15;  // stock Umbra: ~6-7 days a charge
constexpr int kScaleDrainMinPctPerDay = 1;
constexpr int kScaleDrainMaxPctPerDay = 30;
// take_calibration_result(): none due / due but unmeasurable / 1..30 %/day.
constexpr int kCalibrationNone = -1;
constexpr int kCalibrationTooShort = 0;

// Everything that must survive a reboot (see pack/unpack below).
struct ScaleBatteryState {
  bool have_live = false;      // a reading has ever been seen
  uint8_t live_pct = 0;        // the last live reading
  uint32_t stale_since = 0;    // Unix s when it went stale; 0 = live
  uint32_t last_seen = 0;      // Unix s the scale was last seen (session end)
  uint8_t rate = kScaleDrainDefaultPctPerDay;  // the drain setting, %/day
  bool cal_armed = false;      // the user armed a calibration
  bool cal_running = false;    // armed AND a stale period began after arming
  uint8_t cal_start_pct = 0;   // last-live % when that stale period began
  int8_t cal_result = kCalibrationNone;  // a finished run not yet offered
};

struct ScaleBatteryView {
  bool valid = false;
  int pct = 0;
  bool estimated = false;
};

class ScaleBatteryEstimate {
 public:
  void load(const ScaleBatteryState& s);
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
  // `now` = trusted Unix time, or 0.
  void on_session_end(bool slept, std::time_t now);

  // What to show now (`now` as above).
  ScaleBatteryView view(std::time_t now) const;

  int rate() const { return s_.rate; }
  void set_rate(int pct_per_day);  // clamped to the setting's range

  // Calibration. arm() restarts a run; disarm() cancels one.
  void arm();
  void disarm();
  bool armed() const { return s_.cal_armed; }
  // A finished run's result, handed out once: kCalibrationNone,
  // kCalibrationTooShort, or the measured rate (1..30 %/day, clamped).
  int take_calibration_result();

 private:
  ScaleBatteryState s_;
  bool dirty_ = false;
};

// The stored form: a fixed byte layout (never the struct's memory layout).
//   [0] flags: bit0 have_live, bit1 cal_armed, bit2 cal_running
//   [1] live %  [2] cal start %  [3] cal_result + 1 (0 = none)
//   [4..7] stale_since LE  [8..11] last_seen LE
//   [12..15] tag: FNV-1a 32 of the scale's MAC string, LE
//   [16] rate %/day  [17..19] reserved (0)
constexpr int kScaleBatteryBlobLen = 20;
void pack_scale_battery(const ScaleBatteryState& s, const char* mac,
                        uint8_t out[kScaleBatteryBlobLen]);
// False (and *s untouched) on a wrong length or a different scale's tag.
bool unpack_scale_battery(const uint8_t* data, int len, const char* mac,
                          ScaleBatteryState* s);

}  // namespace core
