#pragma once

#include <cstdint>

// Audio-cue port. The UI never describes a *sound* — it names an EVENT
// (a button was pressed, the machine finished warming up) and this layer owns
// what that event sounds like. Boards with a speaker path (4.3C / P4: ES8311
// codec over I2S) render the tones; everything else is a stub with
// available() = false and the UI hides the sound settings.
//
// Adding a sound to a new event is three lines: a Cue enum value, its notes in
// the table in src/core/sound.cpp, and a play() call at the event. No platform
// code changes — the driver only knows how to sound a pitch for a duration.
//
// Like every core header this stays free of LVGL/Arduino/NimBLE/SDL.

namespace core {

// One step of a cue: a pitch in Hz held for `ms` milliseconds. hz = 0 is a
// rest. The envelope is the renderer's business — these are the notes, not the
// waveform.
struct Tone {
  float hz;
  uint16_t ms;
  // != 0: a guitar-style bend — ONE strike whose pitch glides linearly to
  // bend_hz by the note's midpoint and back down by its end. (Not two notes:
  // a re-struck envelope mid-bend would read as separate notes.)
  float bend_hz = 0.0f;
};

// Which instrument plays a cue. Naming an instrument (rather than partials and
// envelopes) keeps the sound design here and the synthesis on the board.
enum class Timbre {
  Bell,   // struck and ringing: overtones over a sustaining fundamental. Those
          // overtones are what makes it audible across a room — a small
          // speaker barely reproduces a 350 Hz fundamental, but it has no
          // trouble with the octave and twelfth above it.
  Click,  // a bare percussive tick, no ring
};

// The app's sound vocabulary. Keep the list short and the meanings distinct:
// a remote that beeps at everything gets muted.
enum class Cue {
  ButtonPress,  // tick under every button, the one sound that must feel instant
  Ready,        // the machine has finished warming up (see core::ReadyChime)
};

// One request to the speaker: what to sound, how, and how loudly.
struct Playback {
  const Tone* notes = nullptr;
  int count = 0;
  int priority = 0;
  Timbre timbre = Timbre::Click;
  // 0..100, scaling amplitude linearly — 0 is silence, 100 is the voice's own
  // level. Linear rather than perceptual on purpose: the UI shows this number
  // as a percentage, and halving it should halve the signal, not sort-of-halve
  // the loudness. Out-of-range values are clamped.
  int volume = 100;
};

// The notes behind a cue; `count` receives their number. The returned table has
// static lifetime.
const Tone* cue_notes(Cue cue, int& count);

// Which cue wins when two want the speaker at once — higher interrupts lower,
// equal-or-lower is dropped. Keeps a button tick from chopping a chime in half
// while still letting an important cue cut through a trivial one.
int cue_priority(Cue cue);

// The instrument a cue is played on.
Timbre cue_timbre(Cue cue);

// The whole cue, ready to hand to ISound::play().
Playback cue_playback(Cue cue, int volume);

// A one-note audition of a cue — its final note. Cycling a volume setting
// should let you hear the level, not sit through the whole cue on every tap.
Playback cue_sample(Cue cue, int volume);

class ISound {
 public:
  virtual ~ISound() = default;

  // Whether this board can make sound at all (decides if the settings show).
  virtual bool available() const = 0;

  // Sound a playback request. Called from the UI thread — must be cheap and
  // non-blocking (hand off to the player and return), because the button tick
  // rides this path. Implementations copy the notes, so they needn't outlive
  // the call. See cue_priority() for what happens mid-playback.
  virtual void play(const Playback& req) = 0;

  // Play a named cue — the way the UI should normally ask for sound.
  void play(Cue cue, int volume = 100) { play(cue_playback(cue, volume)); }

  // Sound just enough of a cue to judge it (see cue_sample).
  void sample(Cue cue, int volume) { play(cue_sample(cue, volume)); }
};

}  // namespace core
