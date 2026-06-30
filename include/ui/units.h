#pragma once

// Temperature display units. Everything internal (history, set-points, BLE) stays
// Celsius; these convert only at the point of display, driven by the user's
// IDisplaySettings::use_fahrenheit() preference.

namespace ui {

inline float temp_disp(float celsius, bool fahrenheit) {
  return fahrenheit ? celsius * 9.0f / 5.0f + 32.0f : celsius;
}

// "°C" / "°F" — the built-in Montserrat fonts include the degree glyph (0xB0).
inline const char* temp_unit(bool fahrenheit) { return fahrenheit ? "°F" : "°C"; }

}  // namespace ui
