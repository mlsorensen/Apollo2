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

int DoseTracker::container_clash(const float* grams, int n, int skip, float candidate) {
  for (int i = 0; i < n; ++i) {
    if (i == skip || grams[i] <= 0.0f) continue;
    if (fabs_f(grams[i] - candidate) <= kDoseMaxG) return i;
  }
  return -1;
}

float DoseTracker::net_dose(float basis) const {
  // The heaviest stored container not heavier than the basis, if any (an
  // empty container reads as 0 g, not as a container-sized dose).
  float best = 0.0f;
  for (int i = 0; i < n_containers_; ++i) {
    if (containers_[i] <= basis && containers_[i] > best) best = containers_[i];
  }
  return basis - best;
}

float DoseTracker::dose_basis() const {
  return base_valid_ ? last_g_ - base_g_ : last_g_;
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

  // Plateaus. A link drop forgets them (a reconnect starts from the raw
  // reading). A new plateau more than the band away from the current one
  // either ADJUSTS the load on the base (a small step, total still positive:
  // the base stays, so a dose built up in stages is offered whole) or starts
  // a new base: the plateau just left when something landed, the new plateau
  // when something was lifted.
  if (!in.bench_connected) {
    plateau_valid_ = base_valid_ = false;
    was_stable_ = false;
  } else {
    const bool st = in.now_ms - stable_since_ms_ >= kDoseStableMs;
    if (st && !was_stable_) {
      const float p = stable_ref_g_;
      if (!plateau_valid_) {
        plateau_g_ = p;
        plateau_valid_ = true;
      } else if (fabs_f(p - plateau_g_) > kDoseStableBandG) {
        const float q = plateau_g_;
        const float base = base_valid_ ? base_g_ : q;
        const float step = p - q;
        const bool adjust = fabs_f(step) <= kDoseMaxG && p - base > 0.0f;
        base_g_ = adjust ? base : (step < 0.0f ? p : q);
        base_valid_ = true;
        plateau_g_ = p;
      }
    }
    was_stable_ = st;
  }

  DoseClear why = DoseClear::None;
  if (pending_g_ > 0.0f) {
    // The screensaver STARTING (an edge, so a Record right after a wake
    // isn't eaten by the level that's about to drop).
    if (in.screensaver && !saver_was_on_) why = DoseClear::Screensaver;

    // Either scale link DROPPING and staying down for the grace period. A
    // link that has not been up since the record is not a drop.
    auto lost = [&](bool up, bool& seen_up, bool& down, uint32_t& since) {
      if (up) { seen_up = true; down = false; return false; }
      if (!seen_up) return false;
      if (!down) { down = true; since = in.now_ms; return false; }
      return in.now_ms - since >= kDoseLinkLostMs;
    };
    const bool e_lost = lost(in.espresso_up, espresso_seen_up_, espresso_down_, espresso_down_since_);
    const bool b_lost = lost(in.bench_up, bench_seen_up_, bench_down_, bench_down_since_);
    if (why == DoseClear::None && (e_lost || b_lost)) why = DoseClear::ScaleLost;

    // Machine On -> Standby while the power is known on both sides of the edge.
    if (why == DoseClear::None && in.power_known && power_was_known_ && machine_was_on_ &&
        !in.machine_on)
      why = DoseClear::Standby;

    // Backstop.
    if (why == DoseClear::None && in.now_ms - recorded_at_ms_ >= kDoseBackstopMs)
      why = DoseClear::Expired;

    if (why != DoseClear::None) pending_g_ = 0.0f;
  } else if (default_skipped_) {
    // A skipped default comes back on the same walk-away signals (a fresh
    // session), without the recorded dose's backstop.
    auto lost = [&](bool up, bool& seen_up, bool& down, uint32_t& since) {
      if (up) { seen_up = true; down = false; return false; }
      if (!seen_up) return false;
      if (!down) { down = true; since = in.now_ms; return false; }
      return in.now_ms - since >= kDoseLinkLostMs;
    };
    const bool e_lost = lost(in.espresso_up, espresso_seen_up_, espresso_down_, espresso_down_since_);
    const bool b_lost = lost(in.bench_up, bench_seen_up_, bench_down_, bench_down_since_);
    if ((in.screensaver && !saver_was_on_) || e_lost || b_lost ||
        (in.power_known && power_was_known_ && machine_was_on_ && !in.machine_on))
      default_skipped_ = false;
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

bool DoseTracker::reading_stable() const {
  return fed_ && last_connected_ && now_ms_ - stable_since_ms_ >= kDoseStableMs;
}

bool DoseTracker::candidate() const {
  if (!reading_stable()) return false;
  if (pending_g_ > 0.0f) return false;  // a recorded dose locks the card until x / the shot
  const float basis = dose_basis();
  if (basis <= 0.0f) return false;  // something lifted, nothing put down
  const float d = net_dose(basis);
  return d >= kDoseMinG && d <= kDoseMaxG;
}

float DoseTracker::candidate_dose() const {
  return candidate() ? net_dose(dose_basis()) : 0.0f;
}

bool DoseTracker::recorded_showing() const {
  return pending_g_ > 0.0f && last_connected_ &&
         fabs_f(last_g_ - recorded_reading_g_) <= kDoseRecordedBandG;
}

float DoseTracker::record() {
  if (!candidate()) return 0.0f;
  pending_g_ = net_dose(dose_basis());
  recorded_reading_g_ = last_g_;
  recorded_at_ms_ = now_ms_;
  // Downtime starts counting from here, and only for a link seen up first.
  espresso_down_ = bench_down_ = false;
  espresso_seen_up_ = bench_seen_up_ = false;
  return pending_g_;
}

float DoseTracker::pending() const {
  if (pending_g_ > 0.0f) return pending_g_;
  if (default_g_ > 0.0f && !default_skipped_) return default_g_;
  return 0.0f;
}

DoseSource DoseTracker::pending_source() const {
  if (pending_g_ > 0.0f) return DoseSource::kRecorded;
  if (default_g_ > 0.0f && !default_skipped_) return DoseSource::kDefault;
  return DoseSource::kNone;
}

void DoseTracker::clear() {
  if (pending_g_ > 0.0f) pending_g_ = 0.0f;
  else if (default_g_ > 0.0f) {
    default_skipped_ = true;
    espresso_down_ = bench_down_ = false;  // its return counts downtime from here
    espresso_seen_up_ = bench_seen_up_ = false;
  }
}

SpentDose DoseTracker::take_for_shot() {
  SpentDose s{pending(), pending_source()};
  pending_g_ = 0.0f;
  default_skipped_ = false;  // the shot after this one gets the default again
  return s;
}

float brew_ratio(float final_g, float dose_g) {
  if (final_g <= 0.0f || dose_g <= 0.0f) return 0.0f;
  return final_g / dose_g;
}

}  // namespace core
