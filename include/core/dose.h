#pragma once

#include <cstdint>

// The dose recorded from the BENCH scale for the next shot, and the brew
// ratio it yields. Pure domain logic: no clock, no storage, no widgets — the
// caller feeds it the bench reading, the walk-away signals and a millisecond
// tick, and reads back what the card and the graph chip should show.
//
// Rules, settled with the owner (2026-10-30, see the dose-ratio design):
//   - CONTAINERS, no mode switch at record time. Up to three stored container
//     weights (a portafilter, a dosing cup). dose = reading minus the heaviest
//     stored container NOT HEAVIER than the reading; none below it = the
//     reading IS the dose (a tared cup); an empty container is 0 g. Every container must weigh at least
//     kContainerMinG, and any two containers more than kDoseMaxG apart: with
//     the floor a bare dose (5..30 g) can never exceed a container, and with
//     the spacing a dose in the lighter of two containers can never reach the
//     heavier one, so the rule is unambiguous. Capture refuses the rest
//     (container_ok / container_spacing_ok).
//   - THE STEP, not the reading (owner, 2026-10-31: "the user has to tare
//     before placing the portafilter, unless we detect the jump"). The scale
//     settles on plateaus; the dose basis is the step from the PREVIOUS
//     plateau to the current one, so the scale's zero never matters: a tared
//     scale steps from 0 (same as the raw reading), an untared scale with a
//     cup already on it steps by exactly what was put down, and grinding
//     into an empty portafilter already on the scale steps by the dose. With
//     no earlier plateau (connected with the load already on) the raw
//     reading is the basis, as before. A negative step (something lifted)
//     is never a dose.
//   - CANDIDATE: the reading has held still for kDoseStableMs AND the net
//     dose of its basis is dose-sized (kDoseMinG..kDoseMaxG, singles
//     included). Only then does the bench card offer Record — a milk jug or
//     an empty scale shows nothing.
//   - RECORD stores the net dose as PENDING for the next shot to START, which
//     spends it (take_for_shot). Re-record replaces. While the same weight
//     still sits on the scale the card says "recorded" instead of offering
//     Record again (recorded_showing).
//   - An unused pending dose clears on WALK-AWAY signals: the screensaver
//     starting, either scale link DROPPING and staying down kDoseLinkLostMs
//     (grace for radio blips — and a link already down at Record time is not
//     a drop: an espresso scale asleep while the dose is weighed must not eat
//     it; its downtime counts only once it has been seen up), the machine
//     going to standby, and a kDoseBackstopMs backstop for setups with no
//     saver and a scale that never sleeps. Silent.
//   - Ratio = yield / dose, printed "1:2.0".
// Cost: ~40 B of state, no allocation.

namespace core {

constexpr int kMaxDoseContainers = 3;
constexpr float kContainerMinG = 50.0f;   // Capture floor (see above)
constexpr float kDoseMinG = 5.0f;         // candidate gate, net grams
constexpr float kDoseMaxG = 30.0f;  // the biggest basket (owner: 48 g was offered — never a dose)
constexpr float kDoseStableBandG = 0.2f;  // "held still" = within this band
constexpr uint32_t kDoseStableMs = 1000;
constexpr float kDoseRecordedBandG = 1.0f;  // same weight still on the scale
constexpr uint32_t kDoseLinkLostMs = 30 * 1000;
constexpr uint32_t kDoseBackstopMs = 60 * 60 * 1000;

// Why a pending dose went away this tick (the UI drops the chip on any).
enum class DoseClear : uint8_t { None, Screensaver, ScaleLost, Standby, Expired };

struct DoseInputs {
  float bench_g = 0.0f;
  bool bench_connected = false;
  // Link state for the walk-away rule. A scale that isn't SAVED is not lost:
  // pass true for it.
  bool espresso_up = true;
  bool bench_up = true;
  bool screensaver = false;
  // Machine power as the link reports it; unknown (link down) = no edge.
  bool power_known = false;
  bool machine_on = false;
  uint32_t now_ms = 0;
};

class DoseTracker {
 public:
  // Stored container weights; entries under kContainerMinG are dropped (an
  // older build can't have written one, but the rule must hold regardless).
  void set_containers(const float* grams, int n);
  int container_count() const { return n_containers_; }
  float container(int i) const { return i >= 0 && i < n_containers_ ? containers_[i] : 0.0f; }
  static bool container_ok(float grams) { return grams >= kContainerMinG; }
  // A new container must sit more than kDoseMaxG from every OTHER stored one
  // (`skip` = the slot being rewritten). Returns the clashing slot, or -1.
  static int container_clash(const float* grams, int n, int skip, float candidate);

  // The container rule on a basis (grams put on the bench).
  float net_dose(float basis) const;
  // What the current reading is measured against: the step from the previous
  // plateau when one was seen, else the raw reading (for tests / logs).
  float dose_basis() const;

  // Feed one poll (any cadence) and apply the walk-away rules. Returns why a
  // pending dose was cleared this tick, or None.
  DoseClear tick(const DoseInputs& in);

  // The bench reading has held still for kDoseStableMs (Capture uses this too).
  bool reading_stable() const;
  // What the bench card shows.
  bool candidate() const;          // stable, connected, dose-sized: offer Record
  float candidate_dose() const;    // the dose Record would store right now
  bool recorded_showing() const;   // pending AND the recorded weight still on

  // Record the candidate. Returns the dose stored, 0 if there was no candidate.
  float record();
  bool has_pending() const { return pending_g_ > 0.0f; }
  float pending() const { return pending_g_; }
  void clear() { pending_g_ = 0.0f; }   // the user's tap on the chip
  // A shot is starting: hand over the pending dose (0 = none) and clear it.
  float take_for_shot();

 private:
  float containers_[kMaxDoseContainers] = {};
  int n_containers_ = 0;

  // Stability of the bench reading.
  float last_g_ = 0.0f;
  bool last_connected_ = false;
  float stable_ref_g_ = 0.0f;
  uint32_t stable_since_ms_ = 0;
  uint32_t now_ms_ = 0;
  bool fed_ = false;
  // Plateaus: the reading the scale last held still at, and the one before
  // it — their difference is the step the current load made.
  bool was_stable_ = false;
  float plateau_g_ = 0.0f;
  bool plateau_valid_ = false;
  float prev_plateau_g_ = 0.0f;
  bool prev_plateau_valid_ = false;

  float pending_g_ = 0.0f;
  float recorded_reading_g_ = 0.0f;
  uint32_t recorded_at_ms_ = 0;

  // Walk-away bookkeeping. A link's downtime counts only after it was seen
  // UP since the record (seen_up), so one asleep at Record time can't clear.
  bool espresso_down_ = false, bench_down_ = false;
  bool espresso_seen_up_ = false, bench_seen_up_ = false;
  uint32_t espresso_down_since_ = 0, bench_down_since_ = 0;
  bool saver_was_on_ = false;
  bool power_was_known_ = false, machine_was_on_ = false;
};

// yield / dose; 0 when either is missing. Print as "1:%.1f".
float brew_ratio(float final_g, float dose_g);

}  // namespace core
