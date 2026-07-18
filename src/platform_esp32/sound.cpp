#include "platform_esp32/sound.h"

#include "platform_esp32/board_config.h"

#ifdef BOARD_HAS_AUDIO

#include <Arduino.h>
#include <Wire.h>

#include <cmath>

#include "driver/i2s_std.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "platform_esp32/io_extension.h"

// ES8311 codec bring-up, DAC path only. The register sequence is distilled
// from esp_codec_dev's es8311.c (the driver inside the 4.3C vendor demo),
// flattened for our one fixed configuration: I2S slave, MCLK from the ESP
// (256*fs), standard I2S format, 16-bit, 44.1 kHz. Read-modify-writes are kept
// where the reference does them so untouched reset-default bits survive.

namespace platform {
namespace {

using namespace board;  // kAudio* / kEs8311Addr / kIoExtPaEnable pin constants

constexpr uint32_t kRate = 44100;
constexpr int kClickHz = 1900;        // tick pitch
constexpr float kClickDecayS = 0.0035f;
constexpr float kClickAmp = 0.45f;
constexpr int kClickFrames = static_cast<int>(kRate * 14 / 1000);  // ~14 ms

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

// The I2S engine runs ONLY while a click is playing. Idle, the channel is
// disabled — no MCLK/BCLK toggling, no DMA traffic, no per-descriptor
// interrupts — because this board's BLE has proven sensitive to standing
// internal-RAM/bus load (a continuously-clocking I2S broke Micra + scale
// connects outright on first bring-up). click() enables the channel, queues
// the pre-rendered tick, and arms a one-shot esp_timer that disables the
// channel again once the tail has drained.
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
    chan_cfg.auto_clear = true;  // underrun plays silence, never loops the click
    // Just enough DMA to hold one whole click, so a write never blocks: 4 x 180
    // frames = 720 >= kClickFrames (~617). Roughly half the default footprint.
    chan_cfg.dma_desc_num = 4;
    chan_cfg.dma_frame_num = 180;
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
    // NOT enabled here — see click(). The codec keeps its register config with
    // the clocks stopped and picks the stream back up when they return.

    codec_open();
    codec_set_fs();
    codec_start();

    // Speaker power amp on (vendor demo never touches it — likely hardwired —
    // but EXIO4 is PA_CTRL per the docs and driving it high is harmless).
    io_extension().set(kIoExtPaEnable, true);

    // Pre-render the click: a short decaying sine tick, same sample in both
    // slots (the codec takes the left). PSRAM — i2s_channel_write copies into
    // the DMA descriptors, so the source needn't be DMA-capable.
    click_ = static_cast<int16_t*>(
        heap_caps_malloc(sizeof(int16_t) * 2 * kClickFrames,
                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (click_ == nullptr) click_ = static_cast<int16_t*>(malloc(sizeof(int16_t) * 2 * kClickFrames));
    if (click_ == nullptr) return;
    for (int i = 0; i < kClickFrames; ++i) {
      const float t = static_cast<float>(i) / kRate;
      const float s =
          sinf(2.0f * static_cast<float>(M_PI) * kClickHz * t) * expf(-t / kClickDecayS);
      const int16_t v = static_cast<int16_t>(s * kClickAmp * 32767.0f);
      click_[2 * i] = v;
      click_[2 * i + 1] = v;
    }

    lock_ = xSemaphoreCreateMutex();
    const esp_timer_create_args_t targs = {
        .callback = &Es8311Sound::idle_cb,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "snd_idle",
        .skip_unhandled_events = true,
    };
    if (esp_timer_create(&targs, &idle_timer_) != ESP_OK) return;
    ok_ = true;
    log_i("sound: ES8311 up (44.1 kHz, click %d frames, on-demand I2S)", kClickFrames);
  }

  bool available() const override { return ok_; }

  void click() override {
    if (!ok_) return;
    xSemaphoreTake(lock_, portMAX_DELAY);
    if (!running_ && i2s_channel_enable(tx_) == ESP_OK) running_ = true;
    if (running_) {
      size_t written = 0;  // fits the DMA buffers; returns without blocking
      i2s_channel_write(tx_, click_, sizeof(int16_t) * 2 * kClickFrames, &written, 30);
      esp_timer_stop(idle_timer_);  // (re)arm the stop for after the tail drains
      esp_timer_start_once(idle_timer_, 250 * 1000);
    }
    xSemaphoreGive(lock_);
  }

 private:
  static void idle_cb(void* arg) {
    auto* self = static_cast<Es8311Sound*>(arg);
    xSemaphoreTake(self->lock_, portMAX_DELAY);
    if (self->running_) {
      i2s_channel_disable(self->tx_);
      self->running_ = false;
    }
    xSemaphoreGive(self->lock_);
  }

  i2s_chan_handle_t tx_ = nullptr;
  int16_t* click_ = nullptr;
  esp_timer_handle_t idle_timer_ = nullptr;
  SemaphoreHandle_t lock_ = nullptr;
  bool running_ = false;  // guarded by lock_
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
  void click() override {}
};

NoSound g_sound;

}  // namespace

void sound_begin() {}

core::ISound& sound() { return g_sound; }

}  // namespace platform

#endif  // BOARD_HAS_AUDIO
