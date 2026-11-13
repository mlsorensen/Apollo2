#include "core/scale_battery_estimate.h"

#include "core/system.h"

namespace core {

namespace {
constexpr std::time_t kSecondsPerDay = 24 * 60 * 60;

int clamp_rate(int r) {
  if (r < kScaleDrainMinPctPerDay) return kScaleDrainMinPctPerDay;
  if (r > kScaleDrainMaxPctPerDay) return kScaleDrainMaxPctPerDay;
  return r;
}

uint32_t mac_tag(const char* mac) {
  uint32_t h = 2166136261u;
  for (const char* c = mac != nullptr ? mac : ""; *c != '\0'; ++c) {
    h ^= static_cast<uint8_t>(*c);
    h *= 16777619u;
  }
  return h;
}

void put32(uint8_t* q, uint32_t v) {
  for (int i = 0; i < 4; ++i) q[i] = static_cast<uint8_t>(v >> (8 * i));
}

uint32_t get32(const uint8_t* q) {
  return static_cast<uint32_t>(q[0]) | static_cast<uint32_t>(q[1]) << 8 |
         static_cast<uint32_t>(q[2]) << 16 | static_cast<uint32_t>(q[3]) << 24;
}
}  // namespace

void ScaleBatteryEstimate::load(const ScaleBatteryState& s) {
  s_ = s;
  s_.rate = static_cast<uint8_t>(clamp_rate(s_.rate));
  dirty_ = false;
}

void ScaleBatteryEstimate::on_reading(int pct, std::time_t now) {
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
      // A reading below the start is the measurement itself (the scale
      // restarted part-way down); at or above it, the scale came back
      // charged and the pack is taken to have died when last seen.
      std::time_t span;
      int drop;
      if (pct < s_.cal_start_pct && now > static_cast<std::time_t>(s_.stale_since)) {
        span = now - static_cast<std::time_t>(s_.stale_since);
        drop = s_.cal_start_pct - pct;
      } else {
        span = static_cast<std::time_t>(s_.last_seen) - static_cast<std::time_t>(s_.stale_since);
        drop = s_.cal_start_pct;
      }
      int rate = kCalibrationTooShort;
      if (span >= kCalibrationMinSpanS && drop >= kCalibrationMinDropPct)
        rate = clamp_rate(static_cast<int>((drop * kSecondsPerDay + span / 2) / span));
      s_.cal_drop = static_cast<uint8_t>(drop < 0 ? 0 : drop);
      const std::time_t hours = span > 0 ? span / 3600 : 0;
      s_.cal_hours = static_cast<uint16_t>(hours > 65535 ? 65535 : hours);
      core::logf("ScaleBattery: calibration %d%% over %lds -> %d%%/day\n", drop,
                 static_cast<long>(span), rate);
      s_.cal_result = static_cast<int8_t>(rate);
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

ScaleBatteryView ScaleBatteryEstimate::view(std::time_t now) const {
  ScaleBatteryView v;
  if (!s_.have_live) return v;
  v.valid = true;
  v.pct = s_.live_pct;
  if (s_.stale_since == 0) return v;
  v.estimated = true;
  if (now <= static_cast<std::time_t>(s_.stale_since)) return v;
  const std::time_t elapsed = now - static_cast<std::time_t>(s_.stale_since);
  const std::time_t used = s_.rate * elapsed / kSecondsPerDay;
  v.pct = used >= s_.live_pct ? 0 : s_.live_pct - static_cast<int>(used);
  return v;
}

void ScaleBatteryEstimate::set_rate(int pct_per_day) {
  const int r = clamp_rate(pct_per_day);
  if (r == s_.rate) return;
  s_.rate = static_cast<uint8_t>(r);
  dirty_ = true;
}

void ScaleBatteryEstimate::arm() {
  s_.cal_armed = true;
  s_.cal_running = false;
  s_.cal_result = kCalibrationNone;
  s_.cal_drop = 0;
  s_.cal_hours = 0;
  dirty_ = true;
}

void ScaleBatteryEstimate::disarm() {
  if (!s_.cal_armed && !s_.cal_running) return;
  s_.cal_armed = false;
  s_.cal_running = false;
  dirty_ = true;
}

int ScaleBatteryEstimate::take_calibration_result() {
  const int r = s_.cal_result;
  if (r != kCalibrationNone) {
    s_.cal_result = kCalibrationNone;
    dirty_ = true;
  }
  return r;
}

void pack_scale_battery(const ScaleBatteryState& s, const char* mac,
                        uint8_t out[kScaleBatteryBlobLen]) {
  for (int i = 0; i < kScaleBatteryBlobLen; ++i) out[i] = 0;
  out[0] = static_cast<uint8_t>((s.have_live ? 1 : 0) | (s.cal_armed ? 2 : 0) |
                                (s.cal_running ? 4 : 0));
  out[1] = s.live_pct;
  out[2] = s.cal_start_pct;
  out[3] = static_cast<uint8_t>(s.cal_result + 1);
  put32(out + 4, s.stale_since);
  put32(out + 8, s.last_seen);
  put32(out + 12, mac_tag(mac));
  out[16] = s.rate;
  out[17] = s.cal_drop > 100 ? 100 : s.cal_drop;
  out[18] = static_cast<uint8_t>(s.cal_hours);
  out[19] = static_cast<uint8_t>(s.cal_hours >> 8);
}

bool unpack_scale_battery(const uint8_t* data, int len, const char* mac,
                          ScaleBatteryState* s) {
  if (data == nullptr || len != kScaleBatteryBlobLen) return false;
  if (get32(data + 12) != mac_tag(mac)) return false;
  ScaleBatteryState r;
  r.have_live = (data[0] & 1) != 0;
  r.cal_armed = (data[0] & 2) != 0;
  r.cal_running = (data[0] & 4) != 0;
  r.live_pct = data[1] <= 100 ? data[1] : 100;
  r.cal_start_pct = data[2] <= 100 ? data[2] : 100;
  const int result = static_cast<int>(data[3]) - 1;
  r.cal_result = static_cast<int8_t>(
      result >= kCalibrationNone && result <= kScaleDrainMaxPctPerDay ? result : kCalibrationNone);
  r.stale_since = get32(data + 4);
  r.last_seen = get32(data + 8);
  r.rate = static_cast<uint8_t>(clamp_rate(data[16]));
  r.cal_drop = data[17] <= 100 ? data[17] : 100;
  r.cal_hours = static_cast<uint16_t>(data[18] | (data[19] << 8));
  *s = r;
  return true;
}

}  // namespace core
