# ESP GPIO internal pull resistor support

This document describes the internal GPIO pull-up and pull-down capabilities
of the Espressif devices available in SimulIDE, how firmware register writes
reach the electrical solver, and which paths were verified. The capability
matrix follows the official Espressif datasheets, technical reference manuals,
ESP-IDF SoC descriptions, and the ESP8266 Arduino core.

## Capability matrix

| Device | Pull-capable GPIOs exposed by SimulIDE | Pull-up | Pull-down | Important exceptions |
| --- | --- | --- | --- | --- |
| ESP8266EX | GPIO0-16 | GPIO0-15 | GPIO0-15 hardware wake pull-down; GPIO16 through `INPUT_PULLDOWN_16` | GPIO16 has no internal pull-up; ordinary Arduino `INPUT_PULLDOWN` is not supported on GPIO0-15 |
| ESP32 | GPIO0-19, GPIO21-23, GPIO25-27, GPIO32-33 | Yes | Yes | GPIO34-39 are input-only and have no integrated pulls |
| ESP32-S3 | GPIO0-21, GPIO26-48 | Yes | Yes | Flash, PSRAM, USB, and strapping use can reserve otherwise pull-capable pads |
| ESP32-C3 | GPIO0-21 | Yes | Yes | Flash, USB, and strapping use can reserve otherwise pull-capable pads |

Classic ESP32 GPIO6-11 are pull-capable in silicon and are exposed by the
package, but they are normally occupied by SPI flash. ESP32-S3 GPIO26-37 and
ESP32-C3 GPIO12-17 have similar flash/PSRAM reservations. These restrictions
do not remove the silicon pull resistors, so their register mappings remain
implemented; firmware should not reconfigure a pad that its selected module
uses for memory.

## Firmware and register paths

On the ESP32 variants, Arduino `pinMode(pin, INPUT_PULLUP)` and
`pinMode(pin, INPUT_PULLDOWN)` reach the ESP-IDF GPIO driver. The driver
enables input mode, disables output mode, and selects the pull register
appropriate for the pad. ESP8266 uses its separate Arduino core and register
definitions described below.

### ESP32-S3 and ESP32-C3

Both devices control ordinary digital pulls in their IO_MUX pad register:

- `FUN_PD`, bit 7: pull-down.
- `FUN_PU`, bit 8: pull-up.
- `FUN_IE`, bit 9: input enable.
- `MCU_SEL`, bits 12-14: pad function.

The QEMU bridge forwards the complete IO_MUX window to `Esp32IoMux`, which
maps each register to an `Esp32Pin`. The IO_MUX register cache contains 50
entries so ESP32-S3 GPIO48, whose register is at offset `0xC4` (index 49), is
not dropped at the array boundary.

### Classic ESP32

Non-RTC pads also use IO_MUX bits 7 and 8. RTC-capable pads use the separate
RTC_IO RDE/RUE fields instead, even after the pad is selected for digital GPIO
operation. The bridge therefore forwards exactly `0x3FF48400-0x3FF487FF`
(0x400 bytes) to the classic-only `Esp32RtcIo` module.

The implemented RTC pull descriptors are:

| GPIO | RTC_IO offset | RUE (pull-up) | RDE (pull-down) |
| ---: | ---: | ---: | ---: |
| 25 | `0x84` | bit 27 | bit 28 |
| 26 | `0x88` | bit 27 | bit 28 |
| 33 | `0x8C` | bit 27 | bit 28 |
| 32 | `0x8C` | bit 22 | bit 23 |
| 4 | `0x94` | bit 27 | bit 28 |
| 0 | `0x98` | bit 27 | bit 28 |
| 2 | `0x9C` | bit 27 | bit 28 |
| 15 | `0xA0` | bit 27 | bit 28 |
| 13 | `0xA4` | bit 27 | bit 28 |
| 12 | `0xA8` | bit 27 | bit 28 |
| 14 | `0xAC` | bit 27 | bit 28 |
| 27 | `0xB0` | bit 27 | bit 28 |

GPIO34-39 intentionally have no RUE or RDE descriptors. Writes to their RTC
pad registers must not create a pull resistor.

RTC_IO and IO_MUX can both receive writes during one GPIO configuration
sequence. `Esp32Pin` tracks their state independently. Once a classic RTC pad
is controlled through RTC_IO, its effective pull state comes from RTC RUE/RDE,
not from the last IO_MUX write. This prevents an IO_MUX cleanup write from
accidentally clearing a valid RTC pull.

### ESP8266EX

The QEMU bridge forwards the exact ESP8266 peripheral windows used by pulls:

- RTC: physical `0x60000700`, SimulIDE offset `0x0700`, size `0x100`.
- IO_MUX: physical `0x60000800`, SimulIDE offset `0x0800`, size `0x100`.

For GPIO0-15, IO_MUX bit 7 (`GPFPU`) controls pull-up and bit 6 (`GPFPD`)
controls the hardware wake pull-down. The IO_MUX register mapping is:

| IO_MUX offset | GPIO | IO_MUX offset | GPIO |
| ---: | ---: | ---: | ---: |
| `0x04` | 12 | `0x24` | 8 |
| `0x08` | 13 | `0x28` | 9 |
| `0x0C` | 14 | `0x2C` | 10 |
| `0x10` | 15 | `0x30` | 11 |
| `0x14` | 3 | `0x34` | 0 |
| `0x18` | 1 | `0x38` | 2 |
| `0x1C` | 6 | `0x3C` | 4 |
| `0x20` | 7 | `0x40` | 5 |

GPIO16 is controlled separately by `GPF16` at physical `0x600007A0`:
bit 3 (`GP16FPD`) enables its pull-down. The Arduino core exposes this as
`INPUT_PULLDOWN_16`. GPIO16 has no documented internal pull-up.

## Electrical model

Pulls are real analog conductances in the SimulIDE solver, not forced digital
levels. `IoPin::setPullup()` adds conductance to the pin's hidden 3.3 V source;
`IoPin::setPulldown()` adds conductance to simulator ground. ESP pads use a
nominal resistance of 45 kOhm in either direction.

The input model also has 10 MOhm to ground. Consequently, an otherwise
unloaded 45 kOhm pull-up settles at approximately:

```text
3.3 V * 10 MOhm / (10 MOhm + 45 kOhm) = 3.285 V
```

A 45 kOhm pull-down opposed by an external 68 kOhm resistor to 3.3 V settles
at approximately:

```text
3.3 V * 45 kOhm / (45 kOhm + 68 kOhm) = 1.314 V
```

An external low-impedance source can therefore override the weak internal
pull, as on hardware. Pull-up and pull-down may both be enabled if firmware
sets both bits; the solver then represents both conductances.

Reset and restamping clear both the logical flags and their electrical
conductances. This avoids a pull configured by a previous firmware run
surviving into the next simulation.

The ESP package supply pins are not connected to this pull source. GPIO output
HIGH and internal pull-up currently use the pad model's hidden 3.3 V source
relative to simulator ground.

## Implementation layout

- `third_party/qemu-simulide/hw/misc/esp32-simulide-bridge.c` forwards the
  classic ESP32 RTC_IO and ESP8266 RTC/IO_MUX MMIO windows.
- `src/microsim/cores/qemu/esp32/esp32rtcio.{h,cpp}` stores classic RTC_IO
  register values and decodes all supported RUE/RDE fields.
- `src/microsim/cores/qemu/esp8266/esp8266iomux.{h,cpp}` decodes GPIO0-15
  pull-up and wake pull-down bits.
- `src/microsim/cores/qemu/esp8266/esp8266rtc.{h,cpp}` decodes the GPIO16
  pull-down field.
- `src/microsim/cores/qemu/esp32/esp32pin.{h,cpp}` owns the independent
  IO_MUX/RTC states and applies the effective 45 kOhm pull.
- `src/gui/circuitwidget/iopin.{h,cpp}` provides the generic electrical
  pull-up and pull-down conductances.

The related classic ESP32 `GPIO_IN1` base was corrected from GPIO33 to GPIO32.
Without this correction, `digitalRead(32)` sampled GPIO33 and
`digitalRead(33)` sampled GPIO34 even when their electrical pulls were correct.

## Resolved defects

The implementation addresses several independent faults found during the
end-to-end firmware-to-solver audit:

- Classic ESP32 RTC_IO was an unimplemented QEMU range and was absent from the
  bridge, so RUE/RDE writes for GPIO0, 2, 4, 12-15, 25-27, 32 and 33 were
  discarded before reaching SimulIDE.
- SimulIDE had no classic RTC_IO adapter and therefore could not translate
  shared RTC pad registers into per-pin electrical pulls.
- ESP8266 IO_MUX and the GPIO16 RTC register were not bridged or decoded.
- Pull-down was only a visual state flag; no conductance was stamped into the
  analog solver.
- ESP32-S3 GPIO48 used IO_MUX cache index 49 while the old cache ended at
  index 48, so both pull directions on that boundary pin were discarded.
- The classic `GPIO_IN1` model started at GPIO33 instead of GPIO32.
- Reset cleared pull flags but could leave the old electrical pull conductance
  active for a subsequent firmware run.
- Independent RTC and IO_MUX writes originally competed for one pull flag,
  allowing a later IO_MUX write to clear a valid classic RTC pull.

## Verification results

The QEMU Xtensa and RISC-V system emulators and the complete SimulIDE
application build successfully with these modules. Static checks cover every
descriptor, MMIO bridge range, source-selection rule, unsupported classic
GPIO34-39, reset behavior, and the electrical divider calculation.

Agent-owned Arduino firmware configured the pulls and was run through QEMU and
SimulIDE. Runtime tracing was temporary and removed after validation.

The pull-up phase confirmed all 28 documented classic ESP32 pull-capable pads,
including all 12 RTC pads. ESP32-S3 confirmed all 33 unrestricted test pads,
including GPIO48, while normal boot configuration also exercised the reserved
IO_MUX range. ESP32-C3 exercised all 22 IO_MUX pull paths across the test
firmware and boot configuration. ESP8266 pull-up remained statically verified.

Dedicated pull-down firmware produced the following results:

| Device | Runtime-tested pull-down GPIOs | Result |
| --- | --- | --- |
| ESP32 | 0-5, 12-19, 21-23, 25-27, 32-33 (22 pads) | All enabled; RTC and IO_MUX ownership matched the descriptor table |
| ESP32-S3 | 0-21, 38-48 (33 pads) | All enabled, including boundary GPIO48 |
| ESP32-C3 | 0-11, 18-21 (16 pads) | All enabled through IO_MUX |
| ESP8266EX | module construction and register paths | Static checks and application smoke load passed; no dedicated guest firmware runtime test |

Flash/PSRAM-reserved pads excluded from guest runtime firmware remain covered
by the same complete IO_MUX mappings and static descriptor checks. All four
device example circuits reached QEMU shared-memory creation and `Circuit
Loaded` without crashes.

The runtime trace proves that guest register writes select the expected
electrical pull source. The voltage values above are derived from and checked
against the solver model; this validation did not add a permanent automated
analog probe test.

## Known limitations

- ESP8266 GPIO0-15 pull-down is the documented wake pull-down bit. The stock
  Arduino API does not offer ordinary `INPUT_PULLDOWN` for these pins.
- Flash/PSRAM ownership and contention are not electrically connected to the
  exposed package pads. Firmware can therefore reconfigure a reserved pad in
  simulation even when doing so would stop execution on a physical module.
- Reset-time strapping values are fixed in the QEMU bridge; external circuit
  voltage and reset pull resistors do not currently change boot strapping.
- IO_MUX input-enable is stored but GPIO input reads do not yet enforce its
  disabled state.
- Internal pull resistance varies on real silicon. The model uses one nominal
  45 kOhm value for deterministic analog behavior.

## Official references

- [ESP8266EX Datasheet v7.1](https://www.espressif.com/sites/default/files/documentation/0a-esp8266ex_datasheet_en.pdf)
- [ESP8266 Technical Reference v1.7](https://www.espressif.com/sites/default/files/documentation/esp8266-technical_reference_en.pdf)
- [ESP32 Series Datasheet v5.3](https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf)
- [ESP32 Technical Reference Manual v5.8](https://www.espressif.com/sites/default/files/documentation/esp32_technical_reference_manual_en.pdf)
- [ESP32-S3 Series Datasheet v2.2](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf)
- [ESP32-S3 Technical Reference Manual v1.8](https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf)
- [ESP32-C3 Series Datasheet v2.4](https://www.espressif.com/sites/default/files/documentation/esp32-c3_datasheet_en.pdf)
- [ESP32-C3 Technical Reference Manual v1.4](https://www.espressif.com/sites/default/files/documentation/esp32-c3_technical_reference_manual_en.pdf)
