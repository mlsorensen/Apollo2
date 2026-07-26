#include "platform_host/fake_shot_store.h"

#include <algorithm>
#include <cmath>

namespace host {

namespace {

// FakeClock's fixed "now" (2026-06-15 ~12:55 UTC) — history spreads back from
// here so relative dates in renders look sane.
constexpr int64_t kNow = 1781528100;

// Synthesize one believable espresso shot: weight follows a sigmoid-ish ramp
// (dead time, ramp, taper into the target), flow is its derivative shaped like
// the classic bell. Deterministic per-id "noise" keeps renders reproducible.
core::ShotRecord make_shot(uint32_t id, int64_t end_unix, float target_g,
                           float final_g, uint32_t duration_ms,
                           core::ShotMode mode, bool wired) {
  core::ShotRecord r;
  r.summary.id = id;
  r.summary.unix_time = end_unix;
  r.summary.duration_ms = duration_ms;
  r.summary.target_g = target_g;
  r.summary.final_g = final_g;
  r.summary.avg_gps =
      duration_ms > 0 ? final_g / (static_cast<float>(duration_ms) / 1000.0f) : 0.0f;
  r.summary.mode = mode;
  r.summary.wired = wired;

  const int n = std::min<int>(core::ShotRecord::kSampleCap,
                              static_cast<int>(duration_ms / 100));
  r.n_samples = n;
  const float dur_s = static_cast<float>(duration_ms) / 1000.0f;
  // Sigmoid centered past the preinfusion dead time; k sets ramp steepness.
  const float mid = dur_s * 0.45f;
  const float k = 4.5f / (dur_s * 0.35f);
  const float sig0 = 1.0f / (1.0f + std::exp(k * mid));           // t = 0
  const float sig1 = 1.0f / (1.0f + std::exp(-k * (dur_s - mid)));  // t = end
  for (int i = 0; i < n; ++i) {
    const float t = (static_cast<float>(i) / static_cast<float>(n - 1)) * dur_s;
    const float sig = 1.0f / (1.0f + std::exp(-k * (t - mid)));
    const float base = (sig - sig0) / (sig1 - sig0);  // 0..1 across the shot
    // Deterministic wobble so traces aren't laser-straight.
    const float wob = 0.15f * std::sin(t * 7.3f + static_cast<float>(id)) +
                      0.08f * std::sin(t * 13.1f);
    const float w = std::max(0.0f, base * final_g + (base > 0.05f ? wob : 0.0f));
    r.samples[i].t_ms = static_cast<uint32_t>(t * 1000.0f);
    r.samples[i].weight_g = w;
    // Flow = analytic derivative of the sigmoid, scaled; matches the weight
    // curve well enough for a mock.
    const float dsig = k * sig * (1.0f - sig) / (sig1 - sig0);
    r.samples[i].flow_gps =
        std::max(0.0f, dsig * final_g * (1.0f + 0.1f * std::sin(t * 5.7f)));
  }
  return r;
}

constexpr int64_t kDay = 86400;

}  // namespace

FakeShotStore::FakeShotStore() {
  // Newest first. A mix of on-target, slightly-off, one manual (no target),
  // and one older outlier, spanning ~7 weeks for the 30-day split to matter.
  uint32_t id = 14;
  shots_.push_back(make_shot(id--, kNow - 2 * 3600, 36.0f, 36.2f, 27400,
                             core::ShotMode::kAuto, true));
  shots_.push_back(make_shot(id--, kNow - 1 * kDay, 36.0f, 35.8f, 26100,
                             core::ShotMode::kAuto, true));
  shots_.push_back(make_shot(id--, kNow - 2 * kDay, 36.0f, 36.9f, 29800,
                             core::ShotMode::kDetect, false));
  shots_.push_back(make_shot(id--, kNow - 3 * kDay, 40.0f, 40.4f, 31500,
                             core::ShotMode::kAuto, true));
  shots_.push_back(make_shot(id--, kNow - 5 * kDay, 36.0f, 34.6f, 24000,
                             core::ShotMode::kAuto, true));
  shots_.push_back(make_shot(id--, kNow - 7 * kDay, 0.0f, 41.3f, 33200,
                             core::ShotMode::kManual, false));
  shots_.push_back(make_shot(id--, kNow - 9 * kDay, 36.0f, 36.1f, 27900,
                             core::ShotMode::kAuto, true));
  shots_.push_back(make_shot(id--, kNow - 12 * kDay, 36.0f, 36.5f, 28600,
                             core::ShotMode::kDetect, false));
  shots_.push_back(make_shot(id--, kNow - 16 * kDay, 38.0f, 37.7f, 30100,
                             core::ShotMode::kAuto, true));
  shots_.push_back(make_shot(id--, kNow - 22 * kDay, 36.0f, 35.9f, 25700,
                             core::ShotMode::kAuto, true));
  shots_.push_back(make_shot(id--, kNow - 33 * kDay, 36.0f, 37.8f, 26800,
                             core::ShotMode::kAuto, true));
  shots_.push_back(make_shot(id--, kNow - 38 * kDay, 36.0f, 36.3f, 27200,
                             core::ShotMode::kAuto, true));
  shots_.push_back(make_shot(id--, kNow - 45 * kDay, 36.0f, 33.9f, 22400,
                             core::ShotMode::kDetect, false));
  shots_.push_back(make_shot(id--, kNow - 49 * kDay, 36.0f, 36.0f, 28800,
                             core::ShotMode::kAuto, true));
}

void FakeShotStore::save(const core::ShotRecord& record) {
  if (!available_) return;
  core::ShotRecord r = record;
  r.summary.id = shots_.empty() ? 1 : shots_.front().summary.id + 1;
  shots_.insert(shots_.begin(), r);
}

int FakeShotStore::count() const { return available_ ? static_cast<int>(shots_.size()) : 0; }

int FakeShotStore::list(core::ShotSummary* out, int max, int offset) const {
  if (!available_) return 0;
  int written = 0;
  for (size_t i = offset; i < shots_.size() && written < max; ++i)
    out[written++] = shots_[i].summary;
  return written;
}

bool FakeShotStore::read(uint32_t id, core::ShotRecord& out) const {
  if (!available_) return false;
  for (const auto& s : shots_)
    if (s.summary.id == id) {
      out = s;
      return true;
    }
  return false;
}

bool FakeShotStore::remove(uint32_t id) {
  if (!available_) return false;
  for (auto it = shots_.begin(); it != shots_.end(); ++it) {
    if (it->summary.id == id) {
      shots_.erase(it);
      return true;
    }
  }
  return false;
}

core::ShotStats FakeShotStore::stats(int64_t now_unix) const {
  if (!available_) return {};
  std::vector<core::ShotSummary> sums;
  sums.reserve(shots_.size());
  for (const auto& s : shots_) sums.push_back(s.summary);
  return core::compute_shot_stats(sums.data(), static_cast<int>(sums.size()),
                                  now_unix, stats_since_);
}

}  // namespace host
