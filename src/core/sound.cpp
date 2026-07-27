#include "core/sound.h"

// The sound design, in one place. Everything here is note data — no platform,
// no synthesis. Boards render it; the sim ignores it.

namespace core {
namespace {

// Equal temperament, A4 = 440 Hz.
constexpr float kBb3 = 233.08f;
constexpr float kD4 = 293.66f;
constexpr float kF4 = 349.23f;
constexpr float kBb4 = 466.16f;
constexpr float kD5 = 587.33f;

// Button tick: a single high, very short blip. Not a pitch you hear as a note
// — at 14 ms it reads as a mechanical click, which is the point.
constexpr Tone kButtonPress[] = {{1900.0f, 14}};

// Ready: a B-flat arpeggio that drops to the root and then climbs past it,
// ~1.5 s end to end. Long and distinct enough to carry from another room,
// which is where you are while the machine heats.
constexpr Tone kReady[] = {
    {kF4, 300}, {kD4, 300}, {kBb3, 300}, {kD5, 300}, {kBb4, 300},
};

}  // namespace

const Tone* cue_notes(Cue cue, int& count) {
  switch (cue) {
    case Cue::Ready:
      count = static_cast<int>(sizeof(kReady) / sizeof(kReady[0]));
      return kReady;
    case Cue::ButtonPress:
    default:
      count = static_cast<int>(sizeof(kButtonPress) / sizeof(kButtonPress[0]));
      return kButtonPress;
  }
}

Playback cue_playback(Cue cue, int volume) {
  int count = 0;
  const Tone* notes = cue_notes(cue, count);
  return Playback{notes, count, cue_priority(cue), cue_timbre(cue), volume};
}

Playback cue_sample(Cue cue, int volume) {
  Playback p = cue_playback(cue, volume);
  // The LAST note, not the first: a cue is written to resolve, so its final
  // note is the one that characterizes it — and for the chime it's also the
  // highest, which is what you're really judging when you set a level.
  if (p.count > 1) {
    p.notes += p.count - 1;
    p.count = 1;
  }
  return p;
}

Timbre cue_timbre(Cue cue) {
  switch (cue) {
    case Cue::Ready:        return Timbre::Bell;
    case Cue::ButtonPress:
    default:                return Timbre::Click;
  }
}

int cue_priority(Cue cue) {
  switch (cue) {
    // The chime announces something you walked away from; a stray button tick
    // must not cut it short. It, in turn, may interrupt a tick (inaudible —
    // the tick is over in 14 ms).
    case Cue::Ready:        return 10;
    case Cue::ButtonPress:  return 0;
    default:                return 0;
  }
}

}  // namespace core
