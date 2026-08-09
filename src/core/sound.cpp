#include "core/sound.h"

// The sound design, in one place. Everything here is note data — no platform,
// no synthesis. Boards render it; the sim ignores it.

namespace core {
namespace {

// Equal temperament, A4 = 440 Hz.
constexpr float kE5 = 659.26f;
constexpr float kF5 = 698.46f;
constexpr float kA5 = 880.00f;
constexpr float kB5 = 987.77f;
constexpr float kCs6 = 1108.73f;

// Button tick: a single high, very short blip. Not a pitch you hear as a note
// — at 14 ms it reads as a mechanical click, which is the point.
constexpr Tone kButtonPress[] = {{1900.0f, 14}};

// Ready: the "Buddy Holly" (Weezer) intro riff, as played at the 17th-19th
// fret on the high E and B strings. The guitar's whole-step bend-and-release
// on the second B is rendered as discrete even eighths — C#6 then B5 —
// because that kept the riff on the beat and read better on the bell than a
// pitch glide (auditioned both; Tone::bend_hz remains available). Replaced
// the old B-flat arpeggio because its lower notes didn't carry — everything
// here sits at 660 Hz+, where the small speaker actually has output. A 250 ms
// eighth grid, only the final note rings free; ~2.6 s end to end.
constexpr Tone kReady[] = {
    {kA5, 250}, {kF5, 250}, {kA5, 250}, {kB5, 250}, {kCs6, 250}, {kB5, 250},
    {kA5, 250}, {kF5, 250}, {kE5, 600},
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
