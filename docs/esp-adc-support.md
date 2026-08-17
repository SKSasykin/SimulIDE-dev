# ESP ADC support

This matrix describes the SAR ADC inputs of the ESP devices available in
SimulIDE: the channels, their GPIO pin assignments, the effective
measurement (full-scale) range per attenuation code and the output
resolution. All values are taken from the official Espressif datasheets.

| Device | ADC unit | Channels | Channel to GPIO | Attenuation full-scale (V), codes 0-3 | Resolution |
| --- | --- | --- | --- | --- | --- |
| ESP8266EX | single SAR | 1 (TOUT) | A0/TOUT | fixed 0-1 V (no attenuation) | 10-bit, 8-sample SAR |
| ESP32 | ADC1 | 8 (CH0-7) | GPIO36, GPIO37, GPIO38, GPIO39, GPIO32, GPIO33, GPIO34, GPIO35 | 0.95, 1.25, 1.75, 2.45 | programmable 9/10/11/12-bit |
| ESP32 | ADC2 | 10 (CH0-9) | GPIO4, GPIO0, GPIO2, GPIO15, GPIO13, GPIO12, GPIO14, GPIO27, GPIO25, GPIO26 | 0.95, 1.25, 1.75, 2.45 | programmable 9/10/11/12-bit |
| ESP32-S3 | ADC1 | 10 (CH0-9) | GPIO1 .. GPIO10 | 0.85, 1.10, 1.60, 2.90 | fixed 12-bit |
| ESP32-S3 | ADC2 | 10 (CH0-9) | GPIO11 .. GPIO20 | 0.85, 1.10, 1.60, 2.90 | fixed 12-bit |
| ESP32-C3 | ADC1 | 5 (CH0-4) | GPIO0 .. GPIO4 | 0.75, 1.05, 1.30, 2.50 | fixed 12-bit |
| ESP32-C3 | ADC2 | 1 (CH0) | GPIO5 | 0.75, 1.05, 1.30, 2.50 | fixed 12-bit |

## Implemented scope

- Every SAR ADC access is forwarded from QEMU to SimulIDE through the
  SimulIDE-to-QEMU bridge, which installs a narrow MMIO window over exactly
  the ADC register block of each chip:
  - ESP32: SENS block at `0x3FF48800`, 0x400 bytes.
  - ESP32-S3: SENS block at `0x60008800`, 0x200 bytes.
  - ESP32-C3: APB_SARADC block at `0x60040000`.
  - ESP8266: SAR block at `0x60000D00`, 0x100 bytes.
- ESP32 / ESP32-S3: the classic two-unit flow is modeled. Writing
  `meas_start` with `sar1_start_sar` / `sar2_start_sar` triggered by the
  polling driver converts the enabled channel (selected by the `sarN_en_pad`
  field), publishes the raw value plus `measN_done_sar` back into the start
  register, so `adc_ll_rtc_convert_is_done()` and
  `adc_ll_rtc_get_convert_value()` see the expected data.
- The channel to GPIO lookup uses the datasheet ADC table, so the voltage of
  the GPIO pin driven by the analog circuit is read and scaled to the
  selected attenuation range and resolution.
- Classic ESP32 output width is programmable through the SENS block register
  at offset `0x2C` (`SENS_SAR_START_FORCE_REG`): ADC1 uses bits 1:0 and ADC2
  uses bits 3:2, codes 0-3 select 9, 10, 11 or 12 bits. The reset default is
  12-bit. ESP32-S3 and ESP32-C3 output a fixed 12-bit value.
- ESP32-S3 and ESP32-C3 use their modern peripheral register layouts
  (`sar_meas1_ctrl2`/`sar_meas2_ctrl2` for the S3, `one_time_sample` plus
  `data1_status`/`data2_status`/`int_raw` for the C3).
- ESP8266 exposes a single external SAR input `A0/TOUT`. The voltage on the
  pin is scaled to the 0-1 V input range and encoded into a 10-bit value.
  Starting a conversion (`SAR_START`) writes the eight SAR result slots in
  the SDK-compatible encoding that `analogRead()`/`__analogRead()` in the
  SDK expect, so existing firmware keeps working unchanged.
- The ESP8266 package now exposes the `A0/TOUT` pin (stable id `A0`) in
  addition to the existing GPIO pins.

## Known limitations

- The ESP32-C3 ADC2_CH0 silicon errata is not modeled.
- Contention between ESP32 ADC2 and Wi-Fi is not modeled.
- The ESP8266 internal VDD measurement path is not modeled.

## Official references

- [ESP8266EX Datasheet v7.1](https://www.espressif.com/sites/default/files/documentation/0a-esp8266ex_datasheet_en.pdf)
- [ESP8266 Technical Reference v1.7](https://www.espressif.com/sites/default/files/documentation/esp8266-technical_reference_en.pdf)
- [ESP32 Series Datasheet v5.3](https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf)
- [ESP32 Technical Reference Manual v5.8](https://www.espressif.com/sites/default/files/documentation/esp32_technical_reference_manual_en.pdf)
- [ESP32-S3 Series Datasheet v2.2](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf)
- [ESP32-S3 Technical Reference Manual v1.8](https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf)
- [ESP32-C3 Series Datasheet v2.4](https://www.espressif.com/sites/default/files/documentation/esp32-c3_datasheet_en.pdf)
- [ESP32-C3 Technical Reference Manual v1.4](https://www.espressif.com/sites/default/files/documentation/esp32-c3_technical_reference_manual_en.pdf)
