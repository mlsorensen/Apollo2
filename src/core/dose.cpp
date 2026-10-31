#include "core/dose.h"

namespace core {

namespace {
float fabs_f(float v) { return v < 0.0f ? -v : v; }
}  // namespace

void DoseTracker::set_containers(const float* grams, int n) {
  n_containers_ = 0;
  for (int i = 0; i < n && n_containers_ < kMaxDoseContainers; ++i) {
    if (container_ok(grams[i])) containers_[n_containers_++] = grams[i];
  }
}

float DoseTracker::net_dose(float reading) const {
  // The heaviest stored container lighter than the reading, if any.
  float best = 0.0f;
  for (int i = 0; i < n_containers_; ++i) {
    if (containers_[i] < reading && containers_[i] > best) best = containers_[i];
  }
  return reading - best;
}

DoseClear DoseTracker::tick(const DoseInputs& in) {
  now_ms_ = in.now_ms;

  // Stability: the reading has to hold within the band; a jump (or the link
  // coming and going) restarts the clock.
  if (!fed_ || in.bench_connected != last_connected_ ||
      fabs_f(in.bench_g - stable_ref_g_) > kDoseStableBandG) {
    stable_ref_g_ = in.bench_g;
    stable_since_ms_ = in.now_ms;
  }
  last_g_ = in.bench_g;
  last_connected_ = in.bench_connected;
  fed_ = true;

  DoseClear why = DoseClear::None;
  if (has_pending()) {
    // The screensaver STARTING (an edge, so a Record right after a wake
    // isn't eaten by the level that's about to drop).
    if (in.screensaver && !saver_was_on_) why = DoseClear::Screensaver;

    // Either scale link down for the grace period.
    auto lost = [&](bool up, bool& down, uint32_t& since) {
      if (up) { down = false; return false; }
      if (!down) { down = true; since = in.now_ms; return false; }
      return in.now_ms - since >= kDoseLinkLostMs;
    };
    const bool e_lost = lost(in.espresso_up, espresso_down_, espresso_down_since_);
    const bool b_lost = lost(in.bench_up, bench_down_, bench_down_since_);
    if (why == DoseClear::None && (e_lost || b_lost)) why = DoseClear::ScaleLost;

    // Machine On -> Standby while the power is known on both sides of the edge.
    if (why == DoseClear::None && in.power_known && power_was_known_ && machine_was_on_ &&
        !in.machine_on)
      why = DoseClear::Standby;

    // Backstop.
    if (why == DoseClear::None && in.now_ms - recorded_at_ms_ >= kDoseBackstopMs)
      why = DoseClear::Expired;

    if (why != DoseClear::None) pending_g_ = 0.0f;
  } else {
    // Keep the link bookkeeping honest even with nothing pending, so a link
    // that went down before Record doesn't count its downtime against a
    // later dose: downtime starts counting from the first tick WITH a dose.
    espresso_down_ = bench_down_ = false;
  }

  saver_was_on_ = in.screensaver;
  power_was_known_ = in.power_known;
  machine_was_on_ = in.machine_on;
  return why;
}

bool DoseTracker::candidate() const {
  if (!fed_ || !last_connected_) return false;
  if (now_ms_ - stable_since_ms_ < kDoseStableMs) return false;
  if (recorded_showing()) return false;  // the same weight: "recorded", not Record
  const float d = net_dose(last_g_);
  return d >= kDoseMinG && d <= kDoseMaxG;
}

float DoseTracker::candidate_dose() const { return candidate() ? net_dose(last_g_) : 0.0f; }

bool DoseTracker::recorded_showing() const {
  return has_pending() && last_connected_ &&
         fabs_f(last_g_ - recorded_reading_g_) <= kDoseRecordedBandG;
}

float DoseTracker::record() {
  if (!candidate()) return 0.0f;
  pending_g_ = net_dose(last_g_);
  recorded_reading_g_ = last_g_;
  recorded_at_ms_ = now_ms_;
  espresso_down_ = bench_down_ = false;
  return pending_g_;
}

float DoseTracker::take_for_shot() {
  const float d = pending_g_;
  pending_g_ = 0.0f;
  return d;
}

float brew_ratio(float final_g, float dose_g) {
  if (final_g <= 0.0f || dose_g <= 0.0f) return 0.0f;
  return final_g / dose_g;
}

}  // namespace core
