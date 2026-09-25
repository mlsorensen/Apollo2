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

  // Screen-dim timeout in minutes (0 = never). After this long with no touch
  // input the UI enters the screensaver; the next touch wakes it.
  virtual int screen_timeout_min() const = 0;
  virtual void set_screen_timeout_min(int minutes) = 0;
  // Live screensaver state — NOT persisted; the saved brightness preference is
  // untouched either way. kDim keeps the panel visible for the bouncing-logo
  // saver: 5% where the backlight can PWM, full brightness where it can only
  // switch (the logo must stay visible). kBlank switches the backlight fully
  // off on every board. kOff restores the user's brightness.
  enum class SaverMode { kOff = 0, kDim = 1, kBlank = 2 };
  virtual void set_screensaver(SaverMode mode) = 0;
  // Persisted idle-screen style, as the Settings cycle button orders it:
  // three bouncing artworks over the dimmed screen (Lion is the default;
  // Alternate takes turns between them every few minutes), then Dim (the live UI
  // stays, backlight down) and Off (display off). Takes effect when the
  // screen-dim timeout fires; irrelevant while the timeout is 0. The device
  // stores it as two NVS keys (style + artwork) so older builds keep reading
  // theirs -- see Config::screensaver_style().
  enum SaverStyle : int {
    kSaverLion = 0,
    kSaverApollo = 1,
    kSaverAlternate = 2,
    kSaverDim = 3,
    kSaverOff = 4,
    kSaverStyleCount = 5,
  };
  virtual int screensaver_style() const = 0;
  virtual void set_screensaver_style(int style) = 0;

  // Whether long flash writes visibly disturb this display (RGB-parallel
  // panels scan continuously from PSRAM, which flash writes stall). The OTA
  // install screen shows a "may flicker" note when true. Default: no.
  virtual bool flash_write_disturbs_display() const { return false; }

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

  // Shot-graph X-window growth: 0 = snap (15 s steps), 1 = smooth (the same
  // steps, each eased over ~1.5 s), 2 = continuous (the window tracks the
  // shot's length once past 15 s; the default). Persisted.
  virtual int shot_window_growth() const = 0;
  virtual void set_shot_window_growth(int mode) = 0;

  // Scale battery on the Home SCALE card: 0 = level icon (the default),
  // 1 = percent, 2 = both. Persisted.
  virtual int scale_battery_style() const = 0;
  virtual void set_scale_battery_style(int style) = 0;

  // Home MICRA card shows the auto-standby countdown while one is running
  // (Settings > Micra > Schedule > Show auto-standby timer). Default off.
  virtual bool show_standby_timer() const = 0;
  virtual void set_show_standby_timer(bool on) = 0;

  // Performance overlay: LVGL's on-screen FPS / CPU / render-time monitor. Off by
  // default (it's a diagnostic and covers a screen corner); the UI shows/hides the
  // sysmon label at runtime to match this.
  virtual bool perf_overlay() const = 0;
  virtual void set_perf_overlay(bool on) = 0;

  // Button-press click sound (boards with a speaker — core::ISound). Default on;
  // the row is hidden entirely when the board can't make sound.
  virtual bool click_sound() const = 0;
  virtual void set_click_sound(bool on) = 0;

  // Volume of the warm-up chime (core::Cue::Ready), linear 0..100 —
  // 0 is off, 80 the default. Same speaker requirement as click_sound. Sound
  // preferences live here rather than with the machine because the speaker is
  // a property of the device.
  virtual int ready_chime_volume() const = 0;
  virtual void set_ready_chime_volume(int percent) = 0;

  // Which tune the warm-up chime plays: 0 = off, otherwise 1-based index
  // into core::ready_melody_* ("Blue", "Pink", ...). Default 1 (Blue).
  // Separate from the volume so switching tunes keeps the level.
  virtual int ready_chime_melody() const = 0;
  virtual void set_ready_chime_melody(int melody) = 0;
};

}  // namespace core
