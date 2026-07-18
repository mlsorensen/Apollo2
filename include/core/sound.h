#pragma once

// UI feedback-sound port. Boards with a speaker path (4.3C: ES8311 codec over
// I2S) implement it; everything else is a stub with available() = false and
// the UI hides the sound setting. Like every core header this stays free of
// LVGL/Arduino/NimBLE/SDL.

namespace core {

class ISound {
 public:
  virtual ~ISound() = default;

  // Whether this board can make sound at all (decides if the setting shows).
  virtual bool available() const = 0;

  // Short button-press tick. Called from the UI thread on press — must be
  // cheap and non-blocking (queue into a DMA buffer and return).
  virtual void click() = 0;
};

}  // namespace core
