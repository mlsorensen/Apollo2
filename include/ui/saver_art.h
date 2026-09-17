#pragma once

// The screensaver's bouncing artwork: which drawing, and the LVGL image
// descriptor for it at a given screen tier. The maps themselves are the
// GENERATED img_*.h headers (tools/logo/gen_logo.py); this is the hand-written
// part that ties them to the Idle screen setting.

#include "lvgl.h"
#include "ui/art_map.h"
#include "ui/img_apollo.h"
#include "ui/img_lion.h"

namespace ui {

// Order is the artwork index stored in NVS (`ssart`) -- append only.
enum class SaverArt : int { kLion = 0, kApollo = 1 };
inline constexpr int kSaverArtCount = 2;

// Every artwork's height as tenths of the SCREEN height. Must match
// HEIGHT_TENTHS in tools/logo/gen_logo.py, which prescales the tiers to
// exactly these heights. 6/10 (owner, 4.3C + P4-5 bench, 2026-09-17):
// Apollo's hatching only reads at that size, and the lion grew to match so
// the two styles feel like one saver. Affordable because the saver runs on
// its own screen (see App::start_screensaver) -- at 4/10 on the top layer
// the frame was already the composite beneath the drawing, not the drawing.
inline constexpr int kSaverArtHeightTenths = 6;

inline int saver_art_height(int screen_h) { return screen_h * kSaverArtHeightTenths / 10; }

inline const ArtMap* saver_art_sizes(SaverArt art, int* count) {
  switch (art) {
    case SaverArt::kApollo:
      *count = kApolloSizeCount;
      return kApolloSizes;
    case SaverArt::kLion:
    default:
      *count = kLionSizeCount;
      return kLionSizes;
  }
}

// The map of `art` whose height is closest to target_h (exact for every
// board tier the generator lists, when target_h came from saver_art_height()).
inline const ArtMap& saver_art_map(SaverArt art, int target_h) {
  int count = 0;
  const ArtMap* sizes = saver_art_sizes(art, &count);
  int best = 0;
  for (int i = 1; i < count; ++i) {
    const int d = sizes[i].h - target_h, b = sizes[best].h - target_h;
    if ((d < 0 ? -d : d) < (b < 0 ? -b : b)) best = i;
  }
  return sizes[best];
}

// ONE shared static descriptor, refilled on every call: the saver shows a
// single image at a time and LVGL's image caches are off (LV_CACHE_DEF_SIZE
// 0), so nothing holds on to a previous fill. Built at runtime because nested
// designated initializers are rejected by C++ and LVGL's struct layout makes
// them brittle to hardcode. Callers must re-run lv_image_set_src after each
// call -- the pointer never changes, the contents do.
inline const lv_image_dsc_t* saver_art_dsc(lv_color_format_t cf, int w, int h,
                                           int stride, const void* data,
                                           uint32_t data_size) {
  static lv_image_dsc_t dsc;
  dsc = lv_image_dsc_t{};
  dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  dsc.header.cf = cf;
  dsc.header.w = w;
  dsc.header.h = h;
  dsc.header.stride = stride;
  dsc.data_size = data_size;
  dsc.data = static_cast<const uint8_t*>(data);
  return &dsc;
}

// The map as an A8 image, recolored by LVGL at draw time (the color changes
// on every bounce). See App::set_saver_art for the pre-blend experiment that
// measured this path as not the cost.
inline const lv_image_dsc_t* saver_art_a8(const ArtMap& m) {
  return saver_art_dsc(LV_COLOR_FORMAT_A8, m.w, m.h, m.w, m.map, m.size);
}

}  // namespace ui
