# Board peripherals: the 4.3C's register expander, one ES8311 driver, and battery state from voltage alone

- **Status:** Accepted
- **Area:** hardware
- **First decided:** 2026-07-18 · **Last changed:** 2026-11-07
- **Boards:** S3 4.3C (expander, audio, battery ADC); P4 boards (audio, battery GPIO); every battery board (USB detection)

## Context

The boards reach the same three peripherals in different ways. The S3
4.3C's backlight dimming and battery ADC sit behind an I2C IO expander,
while the P4s use native pins. Both use an ES8311 codec, with the speaker
amplifier enabled either by the expander or by a GPIO. USB-versus-battery
has no usable signal on the P4: `HWCDC::isPlugged()` always reads false on
its USB-Serial-JTAG.

## Decision

- **Config-only.** Every difference is a `board_config.h` constant or
  feature macro. There is one expander driver (`io_extension.cpp`), one
  audio driver (`sound.cpp`), and one battery driver (`battery.cpp`).
- **4.3C expander:** Waveshare's CH32V003-based "IO extension" at I2C
  0x24, the same register scheme as the 7B's (mode 0x02, output 0x03,
  input 0x04, PWM 0x05, ADC 0x06). This is not the 4.3B's CH422G. It gives
  the 4.3C true PWM backlight dimming, and the duty is
  [inverted](#dd-regs). The battery ADC reads through the expander
  (`BOARD_BATTERY_VIA_IOEXT`).
- **Audio:** the ES8311 at 0x18 on the shared I2C bus. `BOARD_AUDIO_PA_IOEXT`
  selects an expander-driven amplifier enable (4.3C) over a native GPIO
  (P4: GPIO53, active-high). The P4 I2S pins are in [Design
  detail](#dd-regs).
- **Battery:** P4 boards read GPIO20 through a ÷3 divider. USB-versus-battery
  is inferred from the battery node's voltage alone on every board: at or
  above `kUsbPowerVolts` (4.15 V), only external power can hold the node
  that high. `HWCDC::isPlugged()` is no longer used anywhere.

## Consequences

- A new board with the same peripherals is a `board_config.h` block, not
  driver code.
- The 4.3C's ADC scale is the vendor formula (3 × 3.3 / 1023), not
  calibrated against a multimeter. The 7B's same-family chip needed
  0.009632, so a 4.3C battery reading may be a few percent off.
- On the P4, plugging or unplugging USB with a battery fitted power-cycles
  the board. That comes from hardware, not firmware (see the P4 USB-plug
  record).

---

## Details

### Design detail
<a id="dd-regs"></a>

| Item | Value |
|---|---|
| 4.3C expander | CH32V003 firmware, I2C 0x24; regs mode 0x02 / out 0x03 / in 0x04 / PWM 0x05 / ADC 0x06 |
| 4.3C backlight | `IoExtension::set_pwm`: percent 0-100 → register 255 - percent×255/100 (higher register value = dimmer) |
| 4.3C ADC read | write the register, then STOP, then read 2 bytes little-endian. The repeated-start combined read errors INVALID_STATE on this board and poisons the bus. |
| 4.3C battery scale | `kBatteryIoExtScale = 0.0096774` V/count (÷3 divider folded in) |
| P4 audio | I2S port 1, MCLK 13, BCLK 12, LRCLK 10, DOUT 9; ES8311 0x18; PA GPIO53 active-high |
| P4 battery | `kBatteryAdc = 20`, divider 3.0 |
| USB detection | `kUsbPowerVolts = 4.15` on every battery board (`battery.cpp`, windowed max) |

### Evidence

- 4.3C bring-up 2026-07-18 (`cc8ebfc`): PWM dimming and the expander's
  battery ADC working on hardware.
- P4 audio and the GPIO20 battery path were confirmed on hardware on
  2026-09-11. The ÷3 divider checks out: raw × 3 = 4.20 V, the LiPo CV
  level, while charging.
- Voltage-only power status replaced `isPlugged()` on 2026-07-19
  (`7bbe9e3`).

### Alternatives and dead ends

- **`HWCDC::isPlugged()`:** non-functional on the P4's USB-Serial-JTAG
  (always false), so a voltage rule that works on every board replaced it.
- **The vendor's 95 % backlight clamp:** Waveshare's demo clamps the
  4.3C's duty to avoid full-off. `set_pwm` maps 0-100 straight through,
  with no clamp.

### History

- 2026-07-18: 4.3C support as a config-only add (`cc8ebfc`).
- 2026-07-19: P4 audio and paddle (`2b2802c`); voltage-only power status
  (`7bbe9e3`).
- 2026-11-07: written as its own record from the project's working notes
  (CLAUDE.md).

### Sources

`include/platform_esp32/board_config.h` (4.3C and P4 blocks);
`src/platform_esp32/io_extension.cpp`, `sound.cpp`, `battery.cpp`; commits
`cc8ebfc`, `2b2802c`, `7bbe9e3`.
