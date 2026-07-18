#pragma once

#include "core/sound.h"

// Host stand-in for the board speaker: reports available so the sim renders
// the "Button sounds" setting row; click() is a no-op (no audio on the host).

namespace host {

class FakeSound : public core::ISound {
 public:
  bool available() const override { return true; }
  void click() override {}
};

}  // namespace host
