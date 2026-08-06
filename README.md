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

Then go to build_XX folder:

```
$ qmake
$ make
```

The `make` step automatically builds the QEMU emulator binaries
(`qemu-system-xtensa` for ESP32, `qemu-system-arm` for STM32) from the
`third_party/qemu-simulide` submodule into `resources/data/bin/`, before the
SimulIDE binary is linked. QEMU is a runtime-only dependency, so a build
failure of the emulator does not stop the main build (run
`./scripts/build_qemu.sh` manually to see the error). When the binaries are
already present and up to date the step is skipped.

In folder build_XX/executables/SimulIDE_x.x.x you will find the executable
and all files needed to run SimulIDE (on macOS this is
`build_XX/executables/SimulIDE_2.0.0-/simulide.app`, with the data folder
inside at `Contents/MacOS/data`).



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


## ESP32 / STM32 emulation (QEMU):

ESP32 (Xtensa) and STM32 (ARM) microcontrollers are emulated by a fork of
QEMU. The fork lives in the git submodule
`third_party/qemu-simulide` (https://github.com/SKSasykin/SimulIDE-qemu),
pinned to commit `fae418e`. Our modifications (committed directly in the
fork) add the SimulIDE shared-memory bridge, the AHB-to-UART-FIFO mapping,
the SDIO slave controller (SLC) and misc build fixes; no external patch file
is needed.

The ESP32 ROM dumps (`data/bin/esp32/rom/bin/*.bin`) are copied automatically
from the fork's `pc-bios/` directory by `scripts/build_qemu.sh` on every
build, so they do not need to be added or committed manually.

For ESP32 you need a 4 MB flash image. The recommended format is the
`merged.bin` produced by Arduino IDE / arduino-cli (bootloader at 0x1000,
partition table at 0x8000, application at 0x10000). SimulIDE pads smaller
valid binaries to 4 MB automatically and shows a clear error if the file is
missing, empty or larger than 4 MB. If the configured firmware file cannot
be found, the bundled example firmware
(`data/bin/esp32/blink.ino.merged.bin`) is used instead, so an empty ESP32
board still boots and blinks.


