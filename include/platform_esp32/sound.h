#pragma once

#include "core/sound.h"

// Board speaker behind core::ISound — a tone renderer for the cues in
// core/sound.h. On BOARD_HAS_AUDIO boards (4.3C and P4: ES8311 codec over I2S;
// PA via the IO extension or a native GPIO per BOARD_AUDIO_PA_IOEXT)
// sound_begin() probes + initializes the codec and starts the player task;
// everywhere else the singleton is a stub with available() = false.

namespace platform {

// Call once from setup(), after Wire is up (display/touch init does that) and,
// on IO-extension boards, after the extension has been begun by the display.
void sound_begin();

core::ISound& sound();  // shared singleton (stub until/unless sound_begin ran)

}  // namespace platform
