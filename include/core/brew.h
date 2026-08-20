#pragma once

#include <cstdint>

// Core port for brew-by-weight: paddle interception + automatic shot stop.
//
// IMPORTANT: stays free of LVGL/Arduino/BLE/SDL like the other core ports.
//
// Two ways into the shot lifecycle, selected by ShotMode (+ the wired-paddle
// setting):
//   WIRED (mode kAuto/kManual with the harness) — the device sits in the
//   Micra's paddle circuit (the 4.3C's isolated DO/DI, or a relay on
//   plain-GPIO boards; core::IPaddle) and shots start/stop on paddle edges,
//   with weight-triggered auto-stop in kAuto.
//   DETECTOR (mode kDetect, or any mode without the harness) — a weight-stream
//   detector (core::ShotDetector) infers shot start/end from the scale alone.
//   No auto-stop, no overshoot learning, no review-reject flash; the target
//   weight is informational only (the stop_hint flash stands in for the
//   auto-stop). Where the paddle HARDWARE exists, the detector path still
//   relays it pass-through — sense + drive, no timer/phase involvement — so a
//   wired rig can use detect mode (or test it) by just flipping the pill.
//
// The WIRED shot lifecycle (core::BrewController):
//   Idle -> (paddle ON edge, shot mode armed, scale connected) Brewing: tare,
//   ESP shot timer starts, graph resets -> target-overshoot reached (or manual
//   paddle OFF): line opened, timer stops -> Review: graph frozen for reading,
//   dismissed by the Reset button or a timeout -> Idle.
// The physical paddle is relayed EDGE-triggered regardless of the shot
// machinery — an ON edge starts the machine, an OFF edge stops it; an
// automation-initiated stop just leaves the line open until the user's next
// edge — with ONE exception: during kReview an ON edge is swallowed (machine
// stays off) and review_reject_seq ticks so the UI can flash the Reset
// button. Automation only re-arms from kIdle, so relaying there would
// silently run a manual shot when the user expected an auto one.
//
// The UNWIRED lifecycle drives the SAME phases from the detector: a confirmed
// detection enters kBrewing with a retroactive timer start (honest duration
// despite the confirm delay); flow-cease stops the timer and enters kSettling
// -> kReview exactly like a paddle cut.
//
// In EVERY mode, a run shorter than the minimum-shot gate (~8s) never reaches
// settle/review — that's a flush/rinse, not a shot, and it drops silently back
// to kIdle (unwired additionally requires a few grams of net gain).

namespace core {

// How shots start/stop (BrewSnapshot::mode; the Home pill cycles it).
//   kAuto   — wired only: paddle edges run the shot, weight auto-stop.
//   kDetect — the weight-stream detector infers start/end; no auto-stop (the
//             stop_hint flash stands in). Works wired (paddle relays pass-
//             through) and unwired alike.
//   kManual — nothing armed: wired rigs still relay the paddle + run the ESP
//             timer on edges; unwired shows the scale's own timer.
// A wired rig offers all three; without a wired paddle kAuto is unavailable
// (a persisted kAuto degrades to kDetect, which snapshot() reports).
enum class ShotMode : uint8_t { kManual = 0, kAuto = 1, kDetect = 2 };

// Cleaning cycles, both of which need the wired relay (there is no other way
// to run the group). Backflush = detergent in a blind filter, pushed through
// the three-way valve by pulsing the group; the pause between pulses is what
// does the work, so the cycle count and timings are fixed rather than tunable.
// Each pulse RUNS for the auto-flush duration (BrewSnapshot::flush_s) so a
// single setting controls every timed group run — and so a machine with
// preinfusion/prebrew, which restarts on every pulse and can swallow a short
// one entirely, can be given longer pulses by raising Auto flush.
constexpr int kBackflushCycles = 10;
constexpr uint32_t kBackflushOffMs = 3000;
// The Home "Flush" button and the backflush pulses both run for the same time
// as the post-shot auto-flush (BrewSnapshot::flush_s), so there's one duration
// to reason about. With auto-flush switched Off there's no duration to borrow,
// so they fall back to this.
constexpr int kManualFlushDefaultS = 3;

// The run time those three share: the auto-flush setting, or the fallback.
inline uint32_t flush_run_ms(int flush_s) {
  return static_cast<uint32_t>(flush_s > 0 ? flush_s : kManualFlushDefaultS) * 1000u;
}

// Where the shot machinery is in its lifecycle (BrewSnapshot::phase).
enum class ShotPhase : uint8_t {
  kIdle,      // ready — the next paddle ON edge / detection starts a shot
  kBrewing,   // automated shot running: monitoring weight toward the target
  kSettling,  // shot stopped; graph still live for a few seconds so the drip
              // tail settles to zero before the freeze (no new shot can arm)
  kReview,    // graph frozen for review until reset/timeout
};

// The shot machinery is always active — wired boards relay the paddle,
// everything else runs the detector — so there is no "unavailable" state.
struct BrewSnapshot {
  bool  paddle_hw;        // paddle hardware exists on this board (the Settings
                          // "Wired paddle" toggle is only built when it does)
  bool  wired_setting;    // the raw "Wired paddle" user setting (drives the
                          // Settings switch; paddle_wired below is the effect)
  bool  paddle_wired;     // paddle edges are the shot-phase source right now:
                          // paddle_hw && the user setting && mode != kDetect.
                          // False = detector-driven (see the header comment)
  bool  paddle_pressed;   // the physical paddle is currently engaged (wired only)
  bool  brewing;          // the line is held closed (machine running via us)
  ShotPhase phase;        // shot lifecycle (see above)
  ShotMode mode;          // EFFECTIVE start/stop mode (kAuto only when wired)
  uint32_t shot_ms;       // ESP-timed shot duration; holds after stop
  bool  baseline_set;     // starting weight confirmed post-tare (kBrewing only;
                          // the UI re-clears the graph here to shed the
                          // cup-placement spike + its inflated Y axis)
  float start_weight_g;   // shot baseline weight. ~0 wired (post-tare); unwired
                          // it's the detector's untared baseline — the UI
                          // subtracts it so the review plot shows shot grams
  float target_weight_g;  // stop target (informational when !paddle_wired)
  float overshoot_g;      // current learned drip/lag compensation
  int   review_hold_s;    // how long kReview lingers before auto-dismissing
  int   detect_lead_in_s; // detect-mode preinfusion lead-in (seconds): how far
                          // before the detector's flow onset the shot actually
                          // started (lever-on -> first drip). Backdates the
                          // timer and the shot plot origin.
  uint32_t review_reject_seq;  // ticks per paddle ON edge swallowed in kReview
                               // (UI flashes the Reset button on a change)
  uint32_t scale_refuse_seq;   // ticks per paddle ON edge refused because the
                               // armed mode (Auto shot / Shot detect) needs the
                               // scale and it isn't connected — the machine
                               // stays off; the UI pops "connect the scale or
                               // pick Manual mode". Standby wake flips are
                               // never refused (no shot involved).
  bool  stop_hint;        // unwired shots only: the running shot has reached the
                          // point where the auto-stop WOULD fire (target -
                          // overshoot, led by ~250ms of current flow for human
                          // reaction time) — the UI flashes "stop the shot now".
                          // Latches true until the shot ends.
  int   flush_s;          // auto-flush run time in seconds (0 = off). Needs
                          // the wired relay: after a finished shot, when the
                          // scale sees the cup come off, wait a beat and run
                          // the group for this long to rinse the puck's surface.
  int   flush_delay_s;    // cup-off -> flush pause (seconds; user setting)

  // --- Drive line + cleaning (see the kBackflush* constants) ---------------
  bool  relay;            // the drive line is usable: paddle hardware + the
                          // "Wired paddle" setting. Unlike paddle_wired this
                          // ignores the shot mode — a detect-mode wired rig
                          // can still flush and backflush.
  bool  clean_ready;      // a flush/backflush could START right now: relay,
                          // machine not in standby, no shot in flight
  bool  manual_flush;     // the Home "Flush" button is holding the line open
  bool  backflush_active; // the backflush sequence is running
  bool  backflush_on;     // its current phase (true = group running, false = pause)
  int   backflush_cycle;  // 1..kBackflushCycles while active (0 otherwise)
  uint32_t backflush_phase_ms;  // ms left in the current on/off phase
  bool  backflush_done;   // the last sequence ran to completion (cleared when
                          // the next one starts, or on cancel)
};

// True when the ESP-side shot timer is the authoritative shot-time source:
// wired (paddle edges drive it), or the detector is armed. Manual-unwired has
// no edge source at all, so displays fall back to the scale's built-in timer.
// One definition — the label writer (update_home) and the fast 10Hz writer
// (pump_scale_chart) must never disagree.
inline bool esp_shot_timer(const BrewSnapshot& b) {
  return b.paddle_wired || b.mode == ShotMode::kDetect;
}

// True while the weight readout should show shot grams (raw minus the
// detector's baseline): unwired shots never tare, so from detection through
// the settle tail the raw reading includes the cup. Wired shots tare at start
// — raw IS shot grams. kReview excluded: the frozen readout is written once
// at the freeze (already net). One definition — the label writer (update_home)
// and the fast writer (pump_scale_chart) must never disagree.
// True while a shot is in flight: running, or stopped but still capturing the
// drip tail (kSettling) — the window where the live weight stream must not be
// disturbed. Taring would shift the baseline the stop/overshoot math runs on;
// dropping the scale link would blind it.
inline bool shot_in_flight(const BrewSnapshot& b) {
  return b.phase == ShotPhase::kBrewing || b.phase == ShotPhase::kSettling;
}

inline bool unwired_net_weight(const BrewSnapshot& b) {
  return !b.paddle_wired && b.baseline_set && shot_in_flight(b);
}

class IBrewController {
 public:
  virtual ~IBrewController() = default;

  // Latest known state. Cheap, synchronous, no blocking I/O.
  virtual BrewSnapshot snapshot() const = 0;

  // Set the target weight (grams) the shot should stop at. Persisted by the
  // implementation. No-op when !available.
  virtual void set_target_weight_g(float grams) = 0;

  // Select how shots start/stop (see ShotMode). kAuto without a wired paddle
  // behaves as (and reports) kDetect. Paddle pass-through keeps working in
  // every mode. Persisted.
  virtual void set_shot_mode(ShotMode mode) = 0;

  // Leave kReview early (the UI's Reset button). No-op outside kReview.
  virtual void dismiss_review() = 0;

  // How long the frozen review lingers before auto-dismissing (seconds). Persisted.
  virtual void set_review_hold_s(int seconds) = 0;

  // Detect-mode preinfusion lead-in (seconds; 0..10). The detector sees the
  // first weight rise; the machine started earlier — this much earlier. Adjust
  // to match the machine's preinfusion. Persisted.
  virtual void set_detect_lead_in_s(int seconds) = 0;

  // The "Wired paddle" setting (only meaningful on boards with paddle hardware;
  // a no-op elsewhere — they are always unwired). Flipping it mid-shot cancels
  // to kIdle. Persisted.
  virtual void set_wired_paddle(bool on) = 0;

  // Auto-flush run time in seconds (0 = off; the UI offers Off/3s/6s). Needs
  // the wired relay — without it there is no drive line to flush with. Persisted.
  virtual void set_flush_s(int seconds) = 0;

  // How long after the cup comes off before the flush runs (seconds; the UI
  // offers 3/6/9/15). Persisted.
  virtual void set_flush_delay_s(int seconds) = 0;

  // Manual group flush (the Home "Flush" button): runs the group for flush_s
  // seconds — the same duration as the post-shot auto-flush, or
  // kManualFlushDefaultS when that's Off. Tapping again mid-run stops it early.
  // No-op unless clean_ready.
  virtual void toggle_manual_flush() = 0;

  // Start the backflush cleaning sequence (kBackflushCycles on/off pulses).
  // False when it can't run right now (snapshot().clean_ready says the same,
  // so the UI can disable the entry before the user taps it).
  virtual bool start_backflush() = 0;

  // Stop a running backflush and open the line. Safe to call when idle.
  virtual void cancel_backflush() = 0;
};

}  // namespace core
