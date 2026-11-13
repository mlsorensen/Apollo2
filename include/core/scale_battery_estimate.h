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
//   - Calibration, armed by the user: measures the next stale period. Start =
//     the last-live % when it goes stale; end = the stale -> live flip, i.e.
//     the scale restarted (its button, or a charger plug-in — both restart
//     it). The reading that ends the run is the measurement when it is BELOW
//     the start (a restart after a day or more, or the plug-in reading of a
//     drained scale): drop = start - reading over the time since going stale.
//     A reading at or above the start (it came back charged, Apollo slept
//     through the plug-in) assumes the scale hit 0 % when it was last seen,
//     as before. Guard rails (owner, 2026-11-13): a run shorter than
//     kCalibrationMinSpanS or a drop under kCalibrationMinDropPct is noise
//     on an integer gauge and is reported as 0, "too short" — the user is
//     told, never silently disarmed. Otherwise the result is offered (Store /
//     Discard) with the measured drop and hours; off-procedure runs are the
//     user's to discard. Arming is the UI's to gate (disabled while the
//     scale's sleep is Off). A pending result is part of the persisted state.
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
// Guard rails for a run ended by a restart reading: anything shorter, or a
// smaller drop, is quantisation noise on a whole-percent gauge.
constexpr long kCalibrationMinSpanS = 24L * 60 * 60;
constexpr int kCalibrationMinDropPct = 10;

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
  uint8_t cal_drop = 0;        // that run's measured drop, %
  uint16_t cal_hours = 0;      // ... over this many hours (0 = unknown)
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
  bool dirty() const { return dirty_; }
  bool take_dirty() {
    const bool d = dirty_;
    dirty_ = false;
    return d;
  }

  // Every battery reading the scale reports. `now` = trusted Unix time, or 0:
  // it only matters for the reading that ends a calibration run.
  void on_reading(int pct, std::time_t now = 0);
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
  // kCalibrationTooShort, or the measured rate (1..30 %/day, clamped). The
  // measurement behind it stays readable until the next arm().
  int take_calibration_result();
  int calibration_drop_pct() const { return s_.cal_drop; }
  int calibration_hours() const { return s_.cal_hours; }

 private:
  ScaleBatteryState s_;
  bool dirty_ = false;
};

// The stored form: a fixed byte layout (never the struct's memory layout).
//   [0] flags: bit0 have_live, bit1 cal_armed, bit2 cal_running
//   [1] live %  [2] cal start %  [3] cal_result + 1 (0 = none)
//   [4..7] stale_since LE  [8..11] last_seen LE
//   [12..15] tag: FNV-1a 32 of the scale's MAC string, LE
//   [16] rate %/day  [17] cal drop %  [18..19] cal hours LE (0 = unknown;
//   both were reserved zeros before v1.4.0)
constexpr int kScaleBatteryBlobLen = 20;
void pack_scale_battery(const ScaleBatteryState& s, const char* mac,
                        uint8_t out[kScaleBatteryBlobLen]);
// False (and *s untouched) on a wrong length or a different scale's tag.
bool unpack_scale_battery(const uint8_t* data, int len, const char* mac,
                          ScaleBatteryState* s);

}  // namespace core
