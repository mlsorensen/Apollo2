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
//     settles on plateaus; the dose basis is the step from a BASE plateau to
//     the current one, so the scale's zero never matters: a tared scale steps
//     from 0 (same as the raw reading), an untared scale with a cup already
//     on it steps by exactly what was put down, and grinding into an empty
//     portafilter already on the scale steps by the dose. With no earlier
//     plateau (connected with the load already on) the raw reading is the
//     basis. A negative step (something lifted) is never a dose.
//   - THE BASE IS STICKY (owner, 2026-11-07: dosing in stages). A load being
//     ADJUSTED — a small step up or down, no more than kDoseMaxG, that leaves
//     the total since the base positive — keeps its base, so beans added to
//     8 g in a cup offer 10 g, not the 2 g top-up, a few beans taken back out
//     offer the remainder, and 32 g brought back down to 28 g offers 28 g.
//     A step bigger than a dose (a container or a loaded portafilter landing)
//     or one that takes the total to zero or below (a lift) starts a new
//     base: the plateau just left for a landing, the new plateau for a lift.
//   - CANDIDATE: the reading has held still for kDoseStableMs AND the net
//     dose of its basis is dose-sized (kDoseMinG..kDoseMaxG, singles
//     included). Only then does the bench card offer Record — a milk jug or
//     an empty scale shows nothing.
//   - RECORD stores the net dose as PENDING for the next shot to START, which
//     spends it (take_for_shot). While a RECORDED dose is pending the card
//     offers nothing more, whatever lands on the bench (owner, 2026-11-07:
//     quiet until the shot; a correction is x on the chip, then Record). A
//     staged DEFAULT never locks it. While the recorded weight still sits on
//     the scale the caption says "recorded" (recorded_showing); once it
//     changes the caption is plain and the card stays locked.
//   - DEFAULT DOSE (owner, 2026-11-01): an opt-in constant for people who
//     dose the same every day, or have no bench scale. It is STAGED whenever
//     nothing is recorded — the chip shows it, every shot spends it — and a
//     recorded dose overrides it for that one shot. The x on a staged
//     default means "no dose for the next shot": it is suppressed until that
//     shot ends or a walk-away clear fires, then staged again. The shot
//     keeps which it was (DoseSource).
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

constexpr int kMaxDoseContainers = 2;  // was 3 until 2026-11-01 (owner: two is plenty)
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

// Where a shot's dose came from — kept with the shot so a ratio can be told
// apart as measured or assumed (owner, 2026-11-01).
enum class DoseSource : uint8_t { kNone = 0, kRecorded = 1, kDefault = 2 };

// What a starting shot takes with it.
struct SpentDose {
  float grams = 0.0f;  // 0 = none
  DoseSource source = DoseSource::kNone;
};

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
  // What the current reading is measured against: the step from the base
  // plateau when one was seen, else the raw reading (for tests / logs).
  float dose_basis() const;

  // Feed one poll (any cadence) and apply the walk-away rules. Returns why a
  // pending dose was cleared this tick, or None.
  DoseClear tick(const DoseInputs& in);

  // The bench reading has held still for kDoseStableMs (Capture uses this too).
  bool reading_stable() const;
  // What the bench card shows.
  bool candidate() const;          // stable, connected, dose-sized, nothing recorded: offer Record
  float candidate_dose() const;    // the dose Record would store right now
  bool recorded_showing() const;   // recorded AND that weight still on (the caption only)

  // Record the candidate. Returns the dose stored, 0 if there was no candidate.
  float record();
  // The default dose (0 = off). Staged whenever nothing is recorded.
  void set_default_dose(float grams) { default_g_ = grams > 0.0f ? grams : 0.0f; }
  float default_dose() const { return default_g_; }
  // What the next shot would take: a recorded dose, else the default unless
  // the user cleared it for this shot.
  bool has_pending() const { return pending_source() != DoseSource::kNone; }
  float pending() const;
  DoseSource pending_source() const;
  // The user's tap on the chip: a recorded dose is dropped; a staged default
  // is skipped for the next shot only.
  void clear();
  // A shot is starting: hand over what's staged and settle up (the recorded
  // dose is spent; a skipped default is staged again for the shot after).
  SpentDose take_for_shot();

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
  // Plateaus: the reading the scale last held still at, and the BASE the
  // current load is measured from (sticky across small adjustments).
  bool was_stable_ = false;
  float plateau_g_ = 0.0f;
  bool plateau_valid_ = false;
  float base_g_ = 0.0f;
  bool base_valid_ = false;

  float pending_g_ = 0.0f;           // the recorded dose (0 = none)
  float recorded_reading_g_ = 0.0f;
  uint32_t recorded_at_ms_ = 0;
  float default_g_ = 0.0f;           // the default dose setting (0 = off)
  bool default_skipped_ = false;     // x on a staged default: not for the next shot

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
