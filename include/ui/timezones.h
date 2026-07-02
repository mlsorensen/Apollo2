#pragma once

// A small, hardcoded set of common timezones for the Settings picker. Each maps a
// friendly label to a POSIX TZ string whose embedded rules let the C library apply
// daylight saving automatically — no IANA tz database needed on the device. The
// stored value is the POSIX string; the picker finds the current index by matching
// it back. Add rows here as needed; keep labels roughly ordered west -> east.

namespace ui {

struct Timezone {
  const char* label;  // shown in the dropdown
  const char* posix;  // POSIX TZ string (persisted, applied via setenv/configTzTime)
};

inline constexpr Timezone kTimezones[] = {
    {"UTC", "UTC0"},
    {"Honolulu", "HST10"},
    {"Anchorage", "AKST9AKDT,M3.2.0,M11.1.0"},
    {"Los Angeles", "PST8PDT,M3.2.0,M11.1.0"},
    {"Phoenix", "MST7"},
    {"Denver", "MST7MDT,M3.2.0,M11.1.0"},
    {"Chicago", "CST6CDT,M3.2.0,M11.1.0"},
    {"Mexico City", "CST6"},
    {"New York", "EST5EDT,M3.2.0,M11.1.0"},
    {"Sao Paulo", "<-03>3"},
    {"Buenos Aires", "<-03>3"},
    {"London", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Berlin / Paris", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Johannesburg", "SAST-2"},
    {"Athens / Helsinki", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Moscow", "MSK-3"},
    {"Dubai", "<+04>-4"},
    {"Mumbai / Kolkata", "IST-5:30"},
    {"Bangkok", "<+07>-7"},
    {"Singapore", "<+08>-8"},
    {"Hong Kong", "HKT-8"},
    {"Shanghai", "CST-8"},
    {"Tokyo", "JST-9"},
    {"Seoul", "KST-9"},
    {"Brisbane", "AEST-10"},
    {"Sydney", "AEST-10AEDT,M10.1.0,M4.1.0"},
    {"Auckland", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
};

inline constexpr int kTimezoneCount =
    static_cast<int>(sizeof(kTimezones) / sizeof(kTimezones[0]));

}  // namespace ui
