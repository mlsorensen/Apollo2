#pragma once

#include <cstddef>
#include <cstdint>

namespace ui {

// One prescaled A8 alpha map of a screensaver artwork (see
// tools/logo/gen_logo.py for the tiers and why they are prescaled).
struct ArtMap {
  int w;
  int h;
  const uint8_t* map;
  size_t size;
};

}  // namespace ui
