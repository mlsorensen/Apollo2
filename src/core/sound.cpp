#include "core/sound.h"

// The sound design, in one place. Everything here is note data — no platform,
// no synthesis. Boards render it; the sim ignores it.

namespace core {
namespace {

// Equal temperament, A4 = 440 Hz.
constexpr float kAb4 = 415.30f;
constexpr float kBb4 = 466.16f;
constexpr float kB4 = 493.88f;
constexpr float kDb5 = 554.37f;
constexpr float kEb5 = 622.25f;
constexpr float kE5 = 659.26f;
constexpr float kF5 = 698.46f;
constexpr float kGb5 = 739.99f;
constexpr float kA5 = 880.00f;
constexpr float kBb5 = 932.33f;
constexpr float kB5 = 987.77f;
constexpr float kFs5 = 739.99f;   // enharmonic Gb5 — named for the sharp keys
constexpr float kG5 = 783.99f;
constexpr float kGs5 = 830.61f;
constexpr float kCs6 = 1108.73f;
constexpr float kDb6 = 1108.73f;  // enharmonic C#6 — named for the flat keys
constexpr float kD6 = 1174.66f;
constexpr float kEb6 = 1244.51f;
constexpr float kE6 = 1318.51f;
constexpr float kGb6 = 1479.98f;

// Button tick: a single high, very short blip. Not a pitch you hear as a note
// — at 14 ms it reads as a mechanical click, which is the point.
constexpr Tone kButtonPress[] = {{1900.0f, 14}};

// "Blue": the "Buddy Holly" (Weezer) intro riff, as played at the 17th-19th
// fret on the high E and B strings. The guitar's whole-step bend-and-release
// on the second B is rendered as discrete even eighths — C#6 then B5 —
// because that kept the riff on the beat and read better on the bell than a
// pitch glide (auditioned both; Tone::bend_hz remains available). Replaced
// the old B-flat arpeggio because its lower notes didn't carry — everything
// here sits at 660 Hz+, where the small speaker actually has output. A 250 ms
// eighth grid, only the final note rings free; ~2.6 s end to end.
constexpr Tone kReadyBlue[] = {
    {kA5, 250}, {kF5, 250}, {kA5, 250}, {kB5, 250}, {kCs6, 250}, {kB5, 250},
    {kA5, 250}, {kF5, 250}, {kE5, 600},
};

// "Pink": 4/4 in G-flat, written at 116 bpm then sped ~30% per review
// (quarter = 400 ms, dotted = 600, half = 800), raised an octave so the
// whole phrase sits where the small speaker has output: rest / Gb5 Gb5 Eb6
// quarters / Db6, Bb5 dotted quarters / half rest / Eb5 Eb5 Db5 quarters /
// Gb5 half. ~5.6 s — a long, singing phrase rather than a jingle.
constexpr Tone kReadyPink[] = {
    {0.0f, 400},  {kGb5, 400}, {kGb5, 400}, {kEb6, 400}, {kDb6, 600},
    {kBb5, 600},  {0.0f, 800}, {kEb5, 400}, {kEb5, 400}, {kDb5, 400},
    {kGb5, 800},
};

// "Human": E major, straight quarters at 150 bpm (400 ms) — with the
// key's sharps applied: G#5 B5 C#6 E6 C#6 B5 G#5 F#5 E5. ~3.6 s.
constexpr Tone kReadyHuman[] = {
    {kGs5, 400}, {kB5, 400}, {kCs6, 400}, {kE6, 400}, {kCs6, 400},
    {kB5, 400},  {kGs5, 400}, {kFs5, 400}, {kE5, 400},
};

// "Gold": all flats save the marked naturals, eighths, written at 90 bpm
// then sped ~30% per review (eighth 256 ms, quarter 513); the transcription
// raised an octave for the speaker — EXCEPT the final Gb5, already up there:
// Gb5
// Eb6 Db6 Bb5 eighths / Gb5 quarter / Ab4 Bb4 B4(nat) eighths / B5(nat) Gb5
// Eb5 Db5 Gb5 F5(nat) Gb5 eighths. 15 notes, ~4.1 s.
constexpr Tone kReadyGold[] = {
    {kGb5, 256}, {kEb6, 256}, {kDb6, 256}, {kBb5, 256}, {kGb5, 513},
    {kAb4, 256}, {kBb4, 256}, {kB4, 256},  {kB5, 256},  {kGb5, 256},
    {kEb5, 256}, {kDb5, 256}, {kGb5, 256}, {kF5, 256},  {kGb5, 256},
};

// "White": 6/8, written at quarter = 160 bpm then sped up ~1.5x per review
// (quarter 280 ms, half 560) — each half + quarter pair is one 6/8 bar:
// Eb5-F5 / G5-Bb5 / D6-Eb6 / G5-F5 / closing Eb5 half. 9 notes, ~3.9 s.
constexpr Tone kReadyWhite[] = {
    {kEb5, 560}, {kF5, 280},  {kG5, 560}, {kBb5, 280}, {kD6, 560},
    {kEb6, 280}, {kG5, 560},  {kF5, 280}, {kEb5, 560},
};

// "Autumn": 4/4 at 120 bpm (eighth 250 ms, quarter 500; "fill measure" =
// hold to the bar line, 1250 after three eighths): Eb6 E6(nat) eighths + Gb6
// quarter x3 fill bar 1 exactly / B5 Db6 Eb6 eighths, Eb6 fill / Eb6 Db6 Db6
// eighths, Db6 fill / Db6 B5 B5 eighths, B5 fill. 17 notes, 8 s.
constexpr Tone kReadyAutumn[] = {
    {kEb6, 250}, {kE6, 250},  {kGb6, 500}, {kGb6, 500}, {kGb6, 500},
    {kB5, 250},  {kDb6, 250}, {kEb6, 250}, {kEb6, 1250},
    {kEb6, 250}, {kDb6, 250}, {kDb6, 250}, {kDb6, 1250},
    {kDb6, 250}, {kB5, 250},  {kB5, 250},  {kB5, 1250},
};

struct ReadyMelody {
  const char* name;
  const Tone* notes;
  int count;
};
constexpr ReadyMelody kReadyMelodies[] = {
    {"Blue", kReadyBlue, static_cast<int>(sizeof(kReadyBlue) / sizeof(Tone))},
    {"Pink", kReadyPink, static_cast<int>(sizeof(kReadyPink) / sizeof(Tone))},
    {"Human", kReadyHuman,
     static_cast<int>(sizeof(kReadyHuman) / sizeof(Tone))},
    {"Gold", kReadyGold, static_cast<int>(sizeof(kReadyGold) / sizeof(Tone))},
    {"White", kReadyWhite, static_cast<int>(sizeof(kReadyWhite) / sizeof(Tone))},
    {"Autumn", kReadyAutumn,
     static_cast<int>(sizeof(kReadyAutumn) / sizeof(Tone))},
};
constexpr int kReadyMelodyCount =
    static_cast<int>(sizeof(kReadyMelodies) / sizeof(kReadyMelodies[0]));

int clamp_melody(int v) {
  return v < 0 ? 0 : (v >= kReadyMelodyCount ? kReadyMelodyCount - 1 : v);
}

}  // namespace

int ready_melody_count() { return kReadyMelodyCount; }

const char* ready_melody_name(int variant) {
  return kReadyMelodies[clamp_melody(variant)].name;
}

Playback ready_melody_playback(int variant, int volume) {
  const ReadyMelody& m = kReadyMelodies[clamp_melody(variant)];
  return Playback{m.notes, m.count, cue_priority(Cue::Ready),
                  cue_timbre(Cue::Ready), volume};
}

Playback ready_melody_sample(int variant, int volume) {
  Playback p = ready_melody_playback(variant, volume);
  // The final note, same reasoning as cue_sample.
  if (p.count > 1) {
    p.notes += p.count - 1;
    p.count = 1;
  }
  return p;
}

const Tone* cue_notes(Cue cue, int& count) {
  switch (cue) {
    case Cue::Ready:
      count = kReadyMelodies[0].count;
      return kReadyMelodies[0].notes;
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
