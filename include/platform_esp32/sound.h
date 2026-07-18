#pragma once

#include "core/sound.h"

// Board speaker behind core::ISound. On BOARD_HAS_AUDIO boards (4.3C: ES8311
// codec over I2S, PA via the IO extension) sound_begin() probes + initializes
// the codec; everywhere else the singleton is a stub with available() = false.

namespace platform {

// Call once from setup(), after Wire is up (display/touch init does that) and,
// on IO-extension boards, after the extension has been begun by the display.
void sound_begin();

core::ISound& sound();  // shared singleton (stub until/unless sound_begin ran)

}  // namespace platform
