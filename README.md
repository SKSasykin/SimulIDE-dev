# SimulIDE 

Electronic Circuit Simulator

**SimulIDE is a simple real time electronic circuit simulator**, intended for hobbyist or students to learn and experiment with analog and digital electronic circuits and microcontrollers.
It supports PIC, AVR, Arduino and other MCUs and MPUs.

**Simplicity, speed and ease of use** are the key features of this simulator.
You can create, simulate and interact with your circuits within minutes, just drag components from the list, drop into the circuit, connect them and push the “power button” to see how it works.

Simulation speed is one of the most relevant characteristics of this simulator.
It has been deeply optimized to achieve excellent speeds and low cpu usage.

SimulIDE also features a code Editor and Debugger for Arduino, GcBasic, PIC asm, AVR asm and others. It is possible to write, compile and do basic debugging with breakpoints, watch registers and global variables.


## Building SimulIDE:

Build dependencies:

 - Qt5 dev packages
 - Qt5Core
 - Qt5Gui
 - Qt5Xml
 - Qt5Widgets
 - Qt5Concurrent
 - Qt5svg dev
 - Qt5 Multimedia dev
 - Qt5 Serialport dev
 - Qt5 qmake
 - Git (to fetch the QEMU submodule)
 - A C/C++ toolchain, python3 with pip, ninja and pkg-config
 - libgcrypt >= 1.8 (ESP32/STM32 emulation; macOS: `brew install libgcrypt`,
   Debian/Ubuntu: `libgcrypt20-dev`, Fedora: `libgcrypt-devel`)
 - Standard QEMU host build deps: glib-2.0 and pixman dev packages

On macOS (Homebrew) everything above is installed with:

```
$ xcode-select --install
$ brew install qt@5 libgcrypt glib pixman ninja pkgconf python3
```

Note: `qt@5` is keg-only, so add its bin dir to your `PATH` first:

```
$ export PATH="/opt/homebrew/opt/qt@5/bin:$PATH"
```

Once installed, initialize the QEMU submodule from the repository root:

```
$ git submodule update --init --recursive
```

Then go to build folder:

```
$ qmake
$ make
```

The `make` step automatically builds the QEMU emulator binaries
(`qemu-system-xtensa` for ESP32 / ESP32-S3 / ESP8266,
`qemu-system-riscv32` for ESP32-C3, `qemu-system-arm` for STM32) from the
`third_party/qemu-simulide` submodule into `resources/data/bin/`, before the
SimulIDE binary is linked. On macOS the emulators are codesigned with the
`com.apple.security.cs.allow-jit` entitlement
(`scripts/qemu-jit.entitlements`); without it QEMU's TCG would hang at
startup on Apple Silicon. QEMU is a runtime-only dependency, so a build
failure of the emulator does not stop the main build (run
`./scripts/build_qemu.sh` manually to see the error). When the binaries are
already present and up to date the step is skipped.

In folder build/executables you will find the executable named as
`simulide-YYMMDD.HHMM` (on macOS this is
`build/executables/simulide-YYMMDD.HHMM.app`, with the data folder inside at
`Contents/MacOS/data`).



## Running SimulIDE:

Run time dependencies:

 - Qt5Core
 - Qt5Gui
 - Qt5Xml
 - Qt5svg
 - Qt5Widgets
 - Qt5Concurrent
 - Qt5 Multimedia
 - Qt5 Multimedia Plugins
 - Qt5 Serialport


No need for installation, place SimulIDE folder wherever you want and run the executable.


## ESP / STM32 emulation (QEMU):

ESP32, ESP32-S3 (Xtensa), ESP32-C3 (RISC-V) and ESP8266 (Xtensa), plus
STM32 (ARM) microcontrollers are emulated by a fork of QEMU. The fork lives
in the git submodule `third_party/qemu-simulide`
(https://github.com/SKSasykin/SimulIDE-qemu). Our
modifications (committed directly in the fork) add the SimulIDE shared-memory
bridge, the AHB-to-UART-FIFO mapping, the SDIO slave controller (SLC),
per-chip bridge variants for ESP32-S3 (`esp32s3-simulide-bridge`) and
ESP32-C3 (`esp32c3-simulide-bridge`) and misc build fixes; no external patch
file is needed.

Supported Espressif controllers: ESP32, ESP32-S3, ESP32-C3 and ESP8266.

![ESP32 DevKit running in SimulIDE](docs/esp32-devkit.png)

The ESP32 ROM dumps (`data/bin/esp/rom/bin/*.bin`) are copied automatically
from the fork's `pc-bios/` directory by `scripts/build_qemu.sh` on every
build, so they do not need to be added or committed manually.

For ESP32 you need a flash image with an exact size of 2, 4, 8 or 16 MB. The
recommended format is the `merged.bin` produced by Arduino IDE / arduino-cli
or `esptool merge_bin` (bootloader at 0x1000, partition table at 0x8000,
application at 0x10000). PlatformIO app-only `firmware.bin` files are
detected automatically and merged with the sibling `bootloader.bin` and
`partitions.bin` into a 4 MB image. SimulIDE pads smaller valid binaries to
4 MB and shows a clear error if the file is missing, empty or not one of the
supported sizes. If the configured firmware file cannot be found, a bundled
example firmware is used instead, so an empty board still boots and blinks:
`data/bin/esp32/blink.ino.merged.bin` for ESP32,
`data/bin/esp32s3/blink.ino.merged.bin` for ESP32-S3 and
`data/bin/esp32c3/blink.ino.merged.bin` for ESP32-C3. ESP8266 uses the
bundled `data/bin/esp8266/blink.bin` firmware.

### ESP I2C and SPI support

General-purpose I2C and SPI controllers supported per device (memory SPI
controllers reserved for flash/PSRAM are excluded):

| Device | I2C | General-purpose SPI | Emulated in SimulIDE |
| --- | --- | --- | --- |
| ESP8266EX | Software GPIO I2C, up to 100 kHz (no hardware controller) | 1 HSPI, master up to 80 MHz / slave up to 20 MHz; fixed GPIO15 CS, GPIO14 CLK, GPIO12 MISO, GPIO13 MOSI | GPIO bit-banged I2C; polling HSPI master through the fixed pins |
| ESP32 | 2 controllers, master/slave, 100/400 kbit/s, up to 5 MHz programmable; GPIO Matrix | SPI2/HSPI and SPI3/VSPI, master up to 80 MHz; IO_MUX or GPIO Matrix | 2 FIFO/command-list polling I2C masters and 2 buffered polling SPI masters with IO_MUX/matrix routing |
| ESP32-S3 | 2 controllers, master/slave, 100/400 and up to 800 kbit/s; GPIO Matrix | SPI2/FSPI and SPI3, master up to 80 MHz / slave up to 60 MHz; SPI2 IO_MUX/matrix, SPI3 matrix only | 2 FIFO/command-list polling I2C masters and 2 buffered polling SPI masters; direct SPI2 pins and GPIO Matrix routing |
| ESP32-C3 | 1 controller, master/slave, 100/400 and up to 800 kbit/s; GPIO Matrix | SPI2/FSPI, master up to 80 MHz / slave up to 60 MHz; IO_MUX or matrix | 1 FIFO/command-list polling I2C master and 1 buffered polling SPI master; direct SPI2 pins and GPIO Matrix routing |

Implemented scope: I2C CPU FIFO access, RSTART/WRITE/READ/STOP/END command
lists, ACK/NACK status, polling interrupt-status registers and interrupt
delivery; SPI CPU data buffers up to 64 bytes, `CMD.USR`, programmable data
length, clock divider, CPOL/CPHA, bit order and automatic CS0. ESP32-S3/C3
use their modern peripheral layouts and GPIO Matrix offsets; ESP8266 exposes
no fictitious hardware I2C controller (software I2C keeps using GPIO).

Not implemented yet: slave operation for both I2C and SPI, DMA, SPI guest
interrupt delivery, SPI command/address/dummy phases, dual/quad/octal
transfers, arbitration and complete timing/error behavior. The current
implementation targets polling-mode master drivers. See
[docs/esp-i2c-spi-support.md](docs/esp-i2c-spi-support.md) for the full
matrix and official Espressif references.

### ESP ADC support

The SAR ADC inputs of all supported Espressif controllers are emulated with
datasheet-correct channel-to-GPIO mappings and effective measurement ranges.
The voltage on the selected GPIO pin is read directly from the analog
circuit and scaled to the chosen attenuation and resolution.

| Device | ADC unit | Channels | Channel to GPIO | Attenuation full-scale (V), codes 0-3 | Resolution |
| --- | --- | --- | --- | --- | --- |
| ESP8266EX | single SAR | 1 (TOUT) | A0/TOUT | fixed 0-1 V | 10-bit, 8-sample SAR |
| ESP32 | ADC1 | 8 (CH0-7) | GPIO36, GPIO37, GPIO38, GPIO39, GPIO32, GPIO33, GPIO34, GPIO35 | 0.95, 1.25, 1.75, 2.45 | programmable 9/10/11/12-bit |
| ESP32 | ADC2 | 10 (CH0-9) | GPIO4, GPIO0, GPIO2, GPIO15, GPIO13, GPIO12, GPIO14, GPIO27, GPIO25, GPIO26 | 0.95, 1.25, 1.75, 2.45 | programmable 9/10/11/12-bit |
| ESP32-S3 | ADC1 | 10 (CH0-9) | GPIO1 .. GPIO10 | 0.85, 1.10, 1.60, 2.90 | fixed 12-bit |
| ESP32-S3 | ADC2 | 10 (CH0-9) | GPIO11 .. GPIO20 | 0.85, 1.10, 1.60, 2.90 | fixed 12-bit |
| ESP32-C3 | ADC1 | 5 (CH0-4) | GPIO0 .. GPIO4 | 0.75, 1.05, 1.30, 2.50 | fixed 12-bit |
| ESP32-C3 | ADC2 | 1 (CH0) | GPIO5 | 0.75, 1.05, 1.30, 2.50 | fixed 12-bit |

Implementing notes:

- ADC register accesses are forwarded from QEMU to SimulIDE through the
  bridge, which installs a narrow MMIO window over each chip's ADC register
  block (ESP32 SENS at `0x3FF48800`, ESP32-S3 SENS at `0x60008800`,
  ESP32-C3 APB_SARADC at `0x60040000`, ESP8266 SAR at `0x60000D00`).
- The polling flow is modeled: on `meas_start` with the start bit set, the
  enabled channel is converted and the raw value plus the done flag are
  published back into the start register, so
  `adc_ll_rtc_convert_is_done()` / `adc_ll_rtc_get_convert_value()` work.
- Classic ESP32 output width is programmable (9/10/11/12-bit) via the SENS
  register at offset `0x2C`; ESP32-S3 and ESP32-C3 are fixed 12-bit.
- ESP8266 exposes the `A0/TOUT` pin (0-1 V range) and writes the eight SAR
  result slots in the SDK-compatible encoding used by `analogRead()`.

Known limitations: the ESP32-C3 ADC2_CH0 silicon errata, ESP32 ADC2/Wi-Fi
contention and the ESP8266 internal VDD measurement are not modeled. See
[docs/esp-adc-support.md](docs/esp-adc-support.md) for the full details and
official Espressif references.
