#include "core/scale_battery_estimate.h"

#include "core/system.h"

namespace core {

namespace {
constexpr std::time_t kSecondsPerDay = 24 * 60 * 60;
}  // namespace

void ScaleBatteryEstimate::on_reading(int pct) {
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  if (!s_.have_live) {
    s_.have_live = true;
    s_.live_pct = static_cast<uint8_t>(pct);
    s_.stale_since = 0;
    dirty_ = true;
    return;
  }
  if (pct == s_.live_pct) return;  // frozen, or a live value that held
  if (s_.stale_since != 0) {
    // A frozen gauge can't change: the scale restarted.
    core::logf("ScaleBattery: live again (%d%% -> %d%%)\n", s_.live_pct, pct);
    if (s_.cal_running) {
      const std::time_t span = static_cast<std::time_t>(s_.last_seen) -
                               static_cast<std::time_t>(s_.stale_since);
      int rate = -1;
      if (span > 0) {
        rate = static_cast<int>((s_.cal_start_pct * kSecondsPerDay + span / 2) / span);
        if (rate < kScaleDrainMinPctPerDay) rate = kScaleDrainMinPctPerDay;
        if (rate > kScaleDrainMaxPctPerDay) rate = kScaleDrainMaxPctPerDay;
      }
      core::logf("ScaleBattery: calibration %d%% over %lds -> %d%%/day\n",
                 s_.cal_start_pct, static_cast<long>(span), rate);
      cal_result_ = rate;
      s_.cal_armed = false;
      s_.cal_running = false;
    }
    s_.stale_since = 0;
    s_.live_pct = static_cast<uint8_t>(pct);
    dirty_ = true;
    return;
  }
  // Live and moving: RAM only. The value is persisted when it goes stale.
  s_.live_pct = static_cast<uint8_t>(pct);
}

void ScaleBatteryEstimate::on_session_end(bool slept, std::time_t now) {
  if (now <= 0) return;  // no trusted clock: nothing to timestamp
  s_.last_seen = static_cast<uint32_t>(now);
  if (slept && s_.have_live && s_.stale_since == 0) {
    s_.stale_since = static_cast<uint32_t>(now);
    if (s_.cal_armed && !s_.cal_running) {
      s_.cal_running = true;
      s_.cal_start_pct = s_.live_pct;
    }
    core::logf("ScaleBattery: stale at %d%%\n", s_.live_pct);
  }
  dirty_ = true;
}

ScaleBatteryView ScaleBatteryEstimate::view(int rate_pct_per_day, std::time_t now) const {
  ScaleBatteryView v;
  if (!s_.have_live) return v;
  v.valid = true;
  v.pct = s_.live_pct;
  if (s_.stale_since == 0) return v;
  v.estimated = true;
  if (now <= static_cast<std::time_t>(s_.stale_since)) return v;
  const std::time_t elapsed = now - static_cast<std::time_t>(s_.stale_since);
  const std::time_t used = rate_pct_per_day * elapsed / kSecondsPerDay;
  v.pct = used >= s_.live_pct ? 0 : s_.live_pct - static_cast<int>(used);
  return v;
}

void ScaleBatteryEstimate::arm() {
  s_.cal_armed = true;
  s_.cal_running = false;
  cal_result_ = -1;
  dirty_ = true;
}

void ScaleBatteryEstimate::disarm() {
  if (!s_.cal_armed && !s_.cal_running) return;
  s_.cal_armed = false;
  s_.cal_running = false;
  dirty_ = true;
}

}  // namespace core
