#pragma once

#include "core/timezones.h"

// The Settings picker's timezone table. The data itself lives in
// core/timezones.h (shared with the WiFi setup page); these aliases keep the
// UI's existing ui::kTimezones spelling.

namespace ui {
using core::Timezone;
using core::kTimezones;
using core::kTimezoneCount;
}  // namespace ui
