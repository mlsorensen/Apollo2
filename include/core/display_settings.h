#pragma once

// Device display preferences port (screen brightness, later color scheme). Like
// IMachine/IProvisioner, the UI depends only on this; the device drives the
// real backlight + NVS, the host fakes it.

namespace core {

class IDisplaySettings {
 public:
  virtual ~IDisplaySettings() = default;

  virtual int brightness() const = 0;          // 0..100
  virtual void set_brightness(int percent) = 0;  // apply + persist
  // Whether the backlight can actually be dimmed (PWM). When false the UI hides
  // the brightness control and the board just holds the backlight at full.
  virtual bool supports_brightness() const = 0;

  // Selected color scheme, as an index into the UI's palette list (ui::theme).
  // The port only persists the choice; the UI owns the palettes + applies them.
  virtual int theme() const = 0;
  virtual void set_theme(int index) = 0;

  // Temperature display units: false = Celsius (default), true = Fahrenheit.
  // Affects only how the UI shows temps; everything internal stays Celsius.
  virtual bool use_fahrenheit() const = 0;
  virtual void set_use_fahrenheit(bool on) = 0;

  // Flow graph: whether to drop negative g/s (default true). Flow is derived from
  // the weight stream; a falling weight (cup removed) is negative, which this floors
  // to zero so it never shows as a spurious upswing.
  virtual bool drop_negative_flow() const = 0;
  virtual void set_drop_negative_flow(bool on) = 0;

  // Flow graph style: true = oscilloscope sweep (a stationary trace a cursor wipes
  // across; the default), false = scrolling strip chart. Sweep repaints only one
  // column per step, so it's far cheaper and tears less on the RGB panel.
  virtual bool scope_graph() const = 0;
  virtual void set_scope_graph(bool on) = 0;

  // Shot-graph line smoothing level: 0 = off, 1 = light (0.15), 2 = medium
  // (0.25), 3 = strong (0.33) — the neighbor weight of the draw-time 3-point
  // kernel. Persisted.
  virtual int flow_smooth() const = 0;
  virtual void set_flow_smooth(int level) = 0;

  // Performance overlay: LVGL's on-screen FPS / CPU / render-time monitor. Off by
  // default (it's a diagnostic and covers a screen corner); the UI shows/hides the
  // sysmon label at runtime to match this.
  virtual bool perf_overlay() const = 0;
  virtual void set_perf_overlay(bool on) = 0;
};

}  // namespace core
