#include "platform_esp32/sound.h"

#include "platform_esp32/board_config.h"

#ifdef BOARD_HAS_AUDIO

#include <Arduino.h>
#include <Wire.h>

#include <atomic>
#include <cmath>

#include "driver/i2s_std.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#if defined(BOARD_AUDIO_PA_IOEXT)
#include "platform_esp32/io_extension.h"
#endif

// ES8311 codec bring-up, DAC path only. The register sequence is distilled
// from esp_codec_dev's es8311.c (the driver inside the 4.3C vendor demo),
// flattened for our one fixed configuration: I2S slave, MCLK from the ESP
// (256*fs), standard I2S format, 16-bit, 44.1 kHz. Read-modify-writes are kept
// where the reference does them so untouched reset-default bits survive.

namespace platform {
namespace {

using namespace board;  // kAudio* / kEs8311Addr / kIoExtPaEnable pin constants

constexpr uint32_t kRate = 44100;
// Render granularity (~3 ms per write). Kept well under the DMA ring's 360
// frames so a write blocks only once the ring still holds ~230 frames (~5 ms)
// — the slack the player task has to wake and refill before an underrun. The
// scratch buffer this sizes lives in PSRAM, so shrinking it costs nothing that
// matters.
constexpr int kChunkFrames = 128;
constexpr int kMaxNotes = 20;      // longest cue we'll copy; longer is truncated
                                   // (20: "Autumn" runs 17 notes)

// Envelope, in fractions of the note's own length so one shape serves both a
// 14 ms tick and a 300 ms chime note: a hair of attack so a note doesn't start
// with a step discontinuity (itself an audible click), an exponential decay,
// and a short release ramp to zero at the very end. The release matters for
// the bell: it still has real amplitude when its slot runs out, and cutting
// that off mid-cycle would put a click between every pair of notes.
constexpr float kAttackFrac = 0.10f;
constexpr float kMaxAttackS = 0.0015f;
constexpr float kReleaseS = 0.006f;

// Voices. The click is one bare sine that dies in a few milliseconds. The bell
// is a struck-metal stack: a fundamental that rings for most of its slot under
// overtones that fade faster, exactly as a real bell's do. The 4.2x partial is
// deliberately inharmonic — that slight beating against the harmonics is the
// "clang" your ear reads as struck metal rather than a tone generator.
struct Partial {
  float ratio;      // multiple of the note's fundamental
  float amp;        // relative level at the strike
  float decay_mul;  // multiple of the note's decay time
};
constexpr Partial kBellPartials[] = {
    {1.00f, 1.00f, 1.00f},
    {2.00f, 0.55f, 0.65f},
    {3.00f, 0.32f, 0.45f},
    {4.20f, 0.16f, 0.22f},
};
constexpr Partial kClickPartials[] = {{1.00f, 1.00f, 1.00f}};

// Per-voice strike level and decay as a fraction of the note. The bell is
// louder and rings far longer than the tick: it has to carry from another room,
// the tick only has to be felt under your fingertip.
//
// These are the level the partials are normalized to at the strike, NOT the
// waveform's peak — the partials don't crest together, so the bell's actual
// peak lands near 0.85 of full scale at 1.07. That's deliberate: it uses
// nearly all the DAC's range (the codec is already at 0 dB, and this is the
// last place to get loudness for free) while keeping headroom so no sample
// ever wraps. Re-measure if you change the partial table.
constexpr float kBellAmp = 1.07f;
constexpr float kBellDecayFrac = 0.55f;
constexpr float kClickAmp = 0.40f;
constexpr float kClickDecayFrac = 0.30f;

bool es_write(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(kEs8311Addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission(true) == 0;
}

uint8_t es_read(uint8_t reg) {
  Wire.beginTransmission(kEs8311Addr);
  Wire.write(reg);
  Wire.endTransmission(true);  // STOP-style read, like the other expander-family I2C
  if (Wire.requestFrom(static_cast<int>(kEs8311Addr), 1) != 1) return 0;
  return static_cast<uint8_t>(Wire.read());
}

// es8311_open(): core setup, slave mode, MCLK from the MCLK pin, DAC ref on.
void codec_open() {
  es_write(0x44, 0x08);  // I2C noise immunity; written twice on purpose (the
  es_write(0x44, 0x08);  // reference notes the first write can be dropped)
  es_write(0x01, 0x30);
  es_write(0x02, 0x00);
  es_write(0x03, 0x10);
  es_write(0x16, 0x24);
  es_write(0x04, 0x10);
  es_write(0x05, 0x00);
  es_write(0x0B, 0x00);
  es_write(0x0C, 0x00);
  es_write(0x10, 0x1F);
  es_write(0x11, 0x7F);
  es_write(0x00, 0x80);
  es_write(0x00, es_read(0x00) & 0xBF);  // slave mode
  es_write(0x01, 0x3F);                  // MCLK from pin, not inverted
  es_write(0x06, es_read(0x06) & ~0x20);  // SCLK not inverted
  es_write(0x13, 0x10);
  es_write(0x1B, 0x0A);
  es_write(0x1C, 0x6A);
  es_write(0x44, 0x58);  // internal reference (ADCL + DACR)
}

// es8311_set_fs(16 bit, I2S normal, 44.1 kHz at MCLK 11.2896 MHz). The coeff
// row for {11289600, 44100}: pre_div 1, pre_multi 1, adc/dac_div 1, fs_mode 0,
// lrck 0x00/0xFF, bclk_div 4, adc/dac_osr 0x10.
void codec_set_fs() {
  es_write(0x09, es_read(0x09) | 0x0C);   // 16-bit
  es_write(0x0A, es_read(0x0A) | 0x0C);
  es_write(0x09, es_read(0x09) & 0xFC);   // standard I2S framing
  es_write(0x0A, es_read(0x0A) & 0xFC);
  es_write(0x02, es_read(0x02) & 0x07);   // pre_div 1, pre_multi x1
  es_write(0x05, 0x00);                   // adc/dac div 1
  es_write(0x03, (es_read(0x03) & 0x80) | 0x10);  // fs_mode 0, adc_osr
  es_write(0x04, (es_read(0x04) & 0x80) | 0x10);  // dac_osr
  es_write(0x07, es_read(0x07) & 0xC0);   // lrck_h
  es_write(0x08, 0xFF);                   // lrck_l
  es_write(0x06, (es_read(0x06) & 0xE0) | 0x03);  // bclk_div 4
}

// es8311_start() for DAC-only work mode + volume + unmute.
void codec_start() {
  es_write(0x00, 0x80);
  es_write(0x01, 0x3F);
  es_write(0x09, es_read(0x09) & 0xBF);          // DAC SDP running
  es_write(0x0A, (es_read(0x0A) & 0xBF) | 0x40);  // ADC SDP stopped (unused)
  es_write(0x17, 0xBF);
  es_write(0x0E, 0x02);
  es_write(0x12, 0x00);
  es_write(0x14, 0x1A);  // (analog mic path bits; harmless with ADC stopped)
  es_write(0x0D, 0x01);
  es_write(0x15, 0x40);
  es_write(0x37, 0x08);
  es_write(0x45, 0x00);
  es_write(0x32, 0xBF);                  // DAC volume ~= 0 dB
  es_write(0x31, es_read(0x31) & 0x9F);  // unmute
}

// The tone renderer behind core::ISound. It knows nothing about buttons or
// boilers — it sounds a pitch for a duration, and core/sound.cpp decides which
// pitches mean what.
//
// Playback runs on its own small task, synthesizing a chunk at a time and
// letting i2s_channel_write() block for pacing. A cue is therefore only as
// expensive as its length (a 1.5 s chime does not need a 260 KB buffer), and
// play() itself just drops a request in a mailbox and returns — it is called
// from the UI thread on every button press.
//
// The I2S engine runs ONLY while something is playing. Idle, the channel is
// disabled — no MCLK/BCLK toggling, no DMA traffic, no per-descriptor
// interrupts — because this board's BLE has proven sensitive to standing
// internal-RAM/bus load (a continuously-clocking I2S broke Micra + scale
// connects outright on first bring-up). The player enables the channel, and a
// one-shot esp_timer disables it again once the tail has drained.
class Es8311Sound : public core::ISound {
 public:
  void begin() {
    // Probe first: a board revision without the codec just leaves sound off.
    Wire.beginTransmission(kEs8311Addr);
    if (Wire.endTransmission(true) != 0) {
      log_i("sound: no ES8311 at 0x%02x — sound off", kEs8311Addr);
      return;
    }

    i2s_chan_config_t chan_cfg =
        I2S_CHANNEL_DEFAULT_CONFIG(static_cast<i2s_port_t>(kAudioI2sPort), I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;  // underrun plays silence, never loops a stale note
    // 3 x 120 = 360 frames, ~8 ms. This ring is DMA-CAPABLE INTERNAL ram, held
    // from boot — the scarcest pool on the P4 (measured 10KB free / 1,140 B
    // largest with WiFi + BLE + a page load active, tight enough to starve the
    // display's GDMA descriptors and BLE's transport). At 4 x 180 it cost
    // 2,880 B of that pool; this halves it to 1,440 B.
    //
    // Kept STATIC rather than allocating per cue: a boot-time allocation always
    // succeeds, where a runtime one would fail exactly when memory is tight and
    // would silently kill sound at random. The player writes kChunkFrames at a
    // time and blocks on a full buffer, so the ring only has to cover the
    // task's scheduling jitter — ~8 ms of slack for a priority-4 task.
    chan_cfg.dma_desc_num = 3;
    chan_cfg.dma_frame_num = 120;
    if (i2s_new_channel(&chan_cfg, &tx_, nullptr) != ESP_OK) return;
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kRate),  // MCLK = 256 * fs
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = static_cast<gpio_num_t>(kAudioMclk),
            .bclk = static_cast<gpio_num_t>(kAudioBclk),
            .ws = static_cast<gpio_num_t>(kAudioLrclk),
            .dout = static_cast<gpio_num_t>(kAudioDout),
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {},
        },
    };
    if (i2s_channel_init_std_mode(tx_, &std_cfg) != ESP_OK) return;
    // NOT enabled here — the player enables it per cue (see take_pending). The
    // codec keeps its register config with the clocks stopped and picks the
    // stream back up when they return.

    codec_open();
    codec_set_fs();
    codec_start();

    // Scratch for one render chunk, same sample in both slots (the codec takes
    // the left). PSRAM — i2s_channel_write copies into the DMA descriptors, so
    // the source needn't be DMA-capable.
    chunk_ = static_cast<int16_t*>(
        heap_caps_malloc(sizeof(int16_t) * 2 * kChunkFrames,
                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (chunk_ == nullptr)
      chunk_ = static_cast<int16_t*>(malloc(sizeof(int16_t) * 2 * kChunkFrames));
    if (chunk_ == nullptr) return;

    lock_ = xSemaphoreCreateMutex();
    const esp_timer_create_args_t targs = {
        .callback = &Es8311Sound::idle_cb,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "snd_idle",
        .skip_unhandled_events = true,
    };
    if (esp_timer_create(&targs, &idle_timer_) != ESP_OK) return;

    // Player task. Priority 4 (above the Arduino loop / LVGL, below the BLE
    // link tasks): it must wake promptly so the button tick stays instant, but
    // it spends nearly all its time blocked inside i2s_channel_write.
    if (xTaskCreate(&Es8311Sound::player_entry, "snd_play", 3584, this, 4,
                    &player_) != pdPASS)
      return;

    // Prime the DAC before the amp comes up. Between PA-on and the first click
    // the on-demand I2S leaves the codec unclocked, and its floating output
    // through the amp is audible STATIC from boot to the first press (heard on
    // the P4; the 4.3C's amp is likely hardwired on with the same window). An
    // enabled channel with nothing queued clocks out zeros (auto_clear), so:
    // run silence now, amp on after, and the normal idle stop parks the
    // channel 250ms later — once the DAC has been clocked its output stays
    // settled through later clock-stops.
    if (i2s_channel_enable(tx_) == ESP_OK) {
      running_ = true;
      esp_timer_start_once(idle_timer_, 250 * 1000);
    }

    // Speaker power amp on. 4.3C: EXIO4 = PA_CTRL via the IO extension (the
    // vendor demo never touches it — likely hardwired — but driving it high is
    // harmless). Native-GPIO boards (P4): a plain active-high enable pin.
#if defined(BOARD_AUDIO_PA_IOEXT)
    io_extension().set(kIoExtPaEnable, true);
#else
    pinMode(kAudioPaPin, OUTPUT);
    digitalWrite(kAudioPaPin, HIGH);
#endif

    ok_ = true;
    log_i("sound: ES8311 up (44.1 kHz, on-demand I2S, tone player)");
  }

  bool available() const override { return ok_; }

  using core::ISound::play;  // keep the play(Cue) convenience overload visible

  void play(const core::Playback& req) override {
    if (!ok_ || req.notes == nullptr || req.count <= 0) return;
    const int volume = req.volume < 0 ? 0 : req.volume > 100 ? 100 : req.volume;
    if (volume == 0) return;  // muted: don't even wake the codec
    const int count = req.count > kMaxNotes ? kMaxNotes : req.count;
    xSemaphoreTake(lock_, portMAX_DELAY);
    // Only take the speaker from something at least as important, and never
    // let a queued cue be displaced by a less important one. Equal priority
    // wins, so re-requesting the same cue restarts it — that's what makes
    // tapping a volume setting audition each level instead of dropping every
    // tap after the first. A lower-priority cue (the button tick under that
    // very tap) is still turned away.
    const bool beats_playing = !playing_ || req.priority >= playing_priority_;
    const bool beats_pending = !have_pending_ || req.priority >= pending_.priority;
    if (beats_playing && beats_pending) {
      for (int i = 0; i < count; ++i) pending_.notes[i] = req.notes[i];
      pending_.count = count;
      pending_.priority = req.priority;
      pending_.timbre = req.timbre;
      pending_.volume = volume;
      have_pending_ = true;
      if (playing_) cancel_.store(true);  // cut the current cue short for this one
    }
    xSemaphoreGive(lock_);
    if (player_ != nullptr) xTaskNotifyGive(player_);
  }

 private:
  struct Request {
    core::Tone notes[kMaxNotes];
    int count = 0;
    int priority = 0;
    core::Timbre timbre = core::Timbre::Click;
    int volume = 100;  // 0..100, already clamped by play()
  };

  static void player_entry(void* arg) { static_cast<Es8311Sound*>(arg)->player_loop(); }

  // Drain the mailbox, then park the I2S channel again.
  void player_loop() {
    for (;;) {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      Request req;
      while (take_pending(req)) render(req);
      xSemaphoreTake(lock_, portMAX_DELAY);
      playing_ = false;
      if (running_) esp_timer_start_once(idle_timer_, 250 * 1000);
      xSemaphoreGive(lock_);
    }
  }

  // Claim the queued request (if any) and make sure the clocks are running.
  bool take_pending(Request& out) {
    bool got = false;
    xSemaphoreTake(lock_, portMAX_DELAY);
    if (have_pending_) {
      out = pending_;
      have_pending_ = false;
      cancel_.store(false);
      playing_ = true;
      playing_priority_ = out.priority;
      esp_timer_stop(idle_timer_);
      if (!running_ && i2s_channel_enable(tx_) == ESP_OK) running_ = true;
      got = running_;
    }
    xSemaphoreGive(lock_);
    return got;
  }

  // Synthesize the sequence chunk by chunk. The write blocks on a full DMA
  // buffer, which is what paces playback to real time — no timers involved.
  void render(const Request& req) {
    const bool bell = req.timbre == core::Timbre::Bell;
    const Partial* partials = bell ? kBellPartials : kClickPartials;
    const int voices = bell ? static_cast<int>(sizeof(kBellPartials) / sizeof(Partial))
                            : static_cast<int>(sizeof(kClickPartials) / sizeof(Partial));
    const float peak = bell ? kBellAmp : kClickAmp;
    const float decay_frac = bell ? kBellDecayFrac : kClickDecayFrac;
    // Scale so the partials sum to `peak` at the strike instead of clipping,
    // then apply the request's volume on top.
    float amp_sum = 0.0f;
    for (int k = 0; k < voices; ++k) amp_sum += partials[k].amp;
    const float gain = peak / amp_sum * (req.volume / 100.0f);

    for (int i = 0; i < req.count && !cancel_.load(); ++i) {
      const core::Tone& note = req.notes[i];
      const int frames = static_cast<int>(kRate * note.ms / 1000);
      const float note_s = static_cast<float>(note.ms) / 1000.0f;
      const float attack_s = fminf(kMaxAttackS, kAttackFrac * note_s);
      const int attack_frames = static_cast<int>(attack_s * kRate);
      const int release_frames =
          static_cast<int>(fminf(kReleaseS, note_s * 0.25f) * kRate);
      const float decay_s = decay_frac * note_s;

      // Per-partial state. The decay is stepped by a constant multiply per
      // frame rather than an expf() per sample — same curve, a fraction of the
      // cost, which matters with four voices at 44.1 kHz.
      float phase[8] = {0.0f};
      float env[8];
      float dphi[8];
      float step[8];
      for (int k = 0; k < voices; ++k) {
        env[k] = partials[k].amp;
        dphi[k] = 2.0f * static_cast<float>(M_PI) * note.hz * partials[k].ratio / kRate;
        step[k] = expf(-1.0f / (kRate * decay_s * partials[k].decay_mul));
      }
      // Guitar-style bend (Tone::bend_hz): ramp each voice's phase increment
      // linearly to the bend pitch by the note's midpoint and back down by its
      // end — one float add per voice per sample, negligible next to the
      // sinf(). One strike, one envelope; only the pitch moves.
      const int half_frames = frames / 2;
      const bool bend = note.bend_hz > 0.0f && note.hz > 0.0f && half_frames > 0;
      float ddphi[8] = {0.0f};
      if (bend) {
        for (int k = 0; k < voices; ++k) {
          ddphi[k] = 2.0f * static_cast<float>(M_PI) * (note.bend_hz - note.hz) *
                     partials[k].ratio / kRate / static_cast<float>(half_frames);
        }
      }

      for (int done = 0; done < frames && !cancel_.load();) {
        const int n = frames - done < kChunkFrames ? frames - done : kChunkFrames;
        for (int j = 0; j < n; ++j) {
          int16_t v = 0;
          if (note.hz > 0.0f) {
            float s = 0.0f;
            for (int k = 0; k < voices; ++k) {
              s += sinf(phase[k]) * env[k];
              phase[k] += dphi[k];
              if (phase[k] > 2.0f * static_cast<float>(M_PI))
                phase[k] -= 2.0f * static_cast<float>(M_PI);
              env[k] *= step[k];
              if (bend) dphi[k] += (done + j < half_frames ? ddphi[k] : -ddphi[k]);
            }
            const int frame = done + j;
            if (frame < attack_frames)
              s *= static_cast<float>(frame) / attack_frames;
            const int left = frames - frame;
            if (left < release_frames) s *= static_cast<float>(left) / release_frames;
            // Clamp, don't wrap: an int16_t cast of an out-of-range float wraps
            // sign and turns a loud note into a burst of noise. The levels
            // above leave headroom, but a partial-table edit shouldn't be able
            // to make that mistake audible.
            v = static_cast<int16_t>(fmaxf(-1.0f, fminf(1.0f, s * gain)) * 32767.0f);
          }
          chunk_[2 * j] = v;
          chunk_[2 * j + 1] = v;
        }
        size_t written = 0;
        if (i2s_channel_write(tx_, chunk_, sizeof(int16_t) * 2 * n, &written,
                              portMAX_DELAY) != ESP_OK)
          return;
        done += n;
      }
    }
  }

  static void idle_cb(void* arg) {
    auto* self = static_cast<Es8311Sound*>(arg);
    xSemaphoreTake(self->lock_, portMAX_DELAY);
    if (self->running_ && !self->playing_) {
      i2s_channel_disable(self->tx_);
      self->running_ = false;
    }
    xSemaphoreGive(self->lock_);
  }

  i2s_chan_handle_t tx_ = nullptr;
  int16_t* chunk_ = nullptr;  // one render chunk, player-task only
  esp_timer_handle_t idle_timer_ = nullptr;
  SemaphoreHandle_t lock_ = nullptr;
  TaskHandle_t player_ = nullptr;
  Request pending_;              // guarded by lock_
  bool have_pending_ = false;    // guarded by lock_
  bool playing_ = false;         // guarded by lock_
  int playing_priority_ = 0;     // guarded by lock_
  std::atomic<bool> cancel_{false};  // read by render(), set under lock_
  bool running_ = false;         // I2S channel enabled; guarded by lock_
  bool ok_ = false;
};

Es8311Sound g_sound;

}  // namespace

void sound_begin() { g_sound.begin(); }

core::ISound& sound() { return g_sound; }

}  // namespace platform

#else  // !BOARD_HAS_AUDIO

namespace platform {
namespace {

class NoSound : public core::ISound {
 public:
  bool available() const override { return false; }
  using core::ISound::play;
  void play(const core::Playback&) override {}
};

NoSound g_sound;

}  // namespace

void sound_begin() {}

core::ISound& sound() { return g_sound; }

}  // namespace platform

#endif  // BOARD_HAS_AUDIO
