# ESP PWM (LEDC) support

This matrix describes the LEDC PWM controllers of the ESP devices available
in SimulIDE: the channels, timer groups, duty resolution, counter/DUTY
widths, register base addresses and the GPIO Matrix output signal indices.
All values are taken from the official Espressif datasheets and the
ESP-IDF `soc` headers.

| Device | PWM module | Channels | Timers | Duty resolution | Counter | Duty register | Register base | GPIO Matrix signals |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| ESP8266EX | none (software PWM only) | — | — | ~8-10 bit (soft) | — | — | — | — |
| ESP32 | LEDC | 16 (8 HS + 8 LS) | 8 (4 HS + 4 LS) | up to 20 bit | 20-bit | 25 bit (21 int + 4 frac) | 0x3FF59000 | 71-86 |
| ESP32-S3 | LEDC | 8 (LS) | 4 | up to 14 bit | 14-bit | 19 bit (15 int + 4 frac) | 0x60019000 | 73-80 |
| ESP32-C3 | LEDC | 6 (LS) | 4 | up to 14 bit | 14-bit | 19 bit (15 int + 4 frac) | 0x60019000 | 45-50 |

Notes on the hardware:

- The PWM frequency is `f_PWM = clk_src · 256 / (clock_divider · 2^duty_res)`
  where `clock_divider` is an 18-bit fixed-point field with 8 fractional bits
  (divide ratio = field / 256). The register bit offset of the field differs
  per chip (ESP32 `S=5`, ESP32-S3/C3 `S=4`), but the fraction is always 8
  bits.
- `duty_res` selects the counter full scale `2^duty_res` (ESP32 field 5 bits,
  with a 20-bit counter; ESP32-S3/C3 field 4 bits -> up to 14 bit).
- On-time ticks are given by the integer part of the DUTY register
  (`DUTY >> 4`); the low 4 bits are a fractional dithering part.
- As on real hardware, channels have no fixed pins: any channel is routed to
  any GPIO pad through the GPIO Matrix using its output signal index
  (`LEDC_HS_SIG_OUT0-7` / `LEDC_LS_SIG_OUT0-7`).

## Implemented scope

- All LEDC channels and timers of ESP32 (16 + 8), ESP32-S3 (8 + 4) and
  ESP32-C3 (6 + 4) are modeled, with the correct per-group register offsets:
  ESP32 has an HS group at `0x0000` and an LS group at `0x00A0` (HS timers at
  `0x140`, LS timers at `0x160`); ESP32-S3/C3 have a single LS group at
  `0x0000` with timers at `0x00A0`. Channel stride is `0x14`.
- The channel/timer binding matches the hardware `TIMER_SEL` field (bits 1:0
  of `CONF0`): ESP32 HS channels select HS timers 0-3 and LS channels select
  LS timers 4-7; ESP32-S3/C3 channels select their single timer group.
- `SIG_OUT_EN`, `IDLE_LV` and `PAUSE`/`RST` of the timer are honored: a
  disabled channel drives its `IDLE_LV`, a paused/reset timer stops.
- Frequency is computed with the fixed-point divider formula above. ESP32
  supports APB (80 MHz) and REF_TICK (1 MHz); ESP32-S3/C3 additionally decode
  the global `apb_clk_sel` mux for APB (80 MHz), RC_FAST (8 MHz) and XTAL
  (40 MHz). `clock_divider` and `duty_res` are extracted from their real bit
  fields.
- Duty spans 0-100 %: `DUTY >> 4` high-phase ticks against the `2^duty_res`
  full scale. The low nibble is synthesized with the hardware's 16-period
  fractional dithering: a value of `N` produces exactly `N` one-tick-longer
  pulses per 16 PWM periods. The `DUTY` register is also readable from
  firmware (`DUTY_R`).
- The LEDC register block is forwarded from QEMU to SimulIDE through the
  bridge (ESP32 at `0x00059000`, ESP32-S3/C3 at `0x00019000`), and every
  output channel is exposed to the GPIO Matrix under its datasheet signal
  index (ESP32 71-86, ESP32-S3 73-80, ESP32-C3 45-50), so routing to any
  GPIO works like real hardware.
- Low-speed channel and timer parameters use their hardware `PARA_UP`
  strobes. High-speed ESP32 parameters update immediately.
- `HPOINT`, timer `VALUE`, duty fade/scale, timer/fade/overflow-count status,
  and `INT_RAW`/`INT_ST`/`INT_ENA`/`INT_CLR` are modeled. LEDC interrupts are
  routed to the family-specific interrupt source.
- GPIO Matrix output selector bits 8:0, per-pad output inversion, output-enable
  selection/inversion, and one-to-many LEDC fan-out are modeled. ESP32-S3/C3
  also model their IO_MUX GPIO function at `0x60009000`.
- Reset rebinds every channel to timer 0, clears matrix subscriptions and IRQ
  state, and cancels stale events. Changing a running timer reschedules it at
  the new period.
- ESP8266 FRC1 is modeled as a 23-bit countdown timer with LOAD rearm,
  `/1`/`/16`/`/256` dividers, one-shot/autoreload behavior and regular IRQ9.
  The ESP8266 machine accepts ELF, Espressif `0xE9`, and flat IRAM images.

## Known limitations

- LEDC fan-out and the SPI, I2C and UART matrix outputs are wired through the
  GPIO Matrix `Esp32OutputSignal`/`Esp32InputSignal` endpoint model. The MCPWM
  and the remaining unimplemented matrix outputs (RMT, I2S, PCNT, TWAI, EMAC,
  SDIO) are still label-only `nullptr` entries with no SimulIDE module.
- ESP8266 has no hardware PWM peripheral. Its FRC1 timer and regular IRQ9 are
  available to self-contained firmware, but stock Arduino `analogWrite()` is
  still unsupported because the machine has no ESP8266 boot ROM, flash-cache
  execution, or `NmiTimSetFunc`/NMI14 dispatch infrastructure.

## Official references

- [ESP32 Series Datasheet v5.3](https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf)
- [ESP32 Technical Reference Manual v5.8](https://www.espressif.com/sites/default/files/documentation/esp32_technical_reference_manual_en.pdf)
- [ESP32-S3 Series Datasheet v2.2](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf)
- [ESP32-S3 Technical Reference Manual v1.8](https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf)
- [ESP32-C3 Series Datasheet v2.4](https://www.espressif.com/sites/default/files/documentation/esp32-c3_datasheet_en.pdf)
- [ESP32-C3 Technical Reference Manual v1.4](https://www.espressif.com/sites/default/files/documentation/esp32-c3_technical_reference_manual_en.pdf)
