# Emulation and component tests

The suite mirrors the documented implementation areas for every supported ESP
family and selected simulator components. Emulator contracts live in
`tests/<mcu>/<direction>/test.json`, component contracts live in
`tests/components/<component>/test.json`, and IDE end-to-end boot tests live in
`tests/ide/`.

Run everything:

```sh
./tests/run-tests.sh
```

Run one family, one area, or only one layer:

```sh
./tests/run-tests.sh esp32
./tests/run-tests.sh esp32-s3 pwm
./tests/run-tests.sh --contracts-only
./tests/run-tests.sh --ide-only
./tests/run-tests.sh --ide-only --executable /path/to/simulide
```

The contract layer checks that each machine still wires the documented module,
register window and QEMU bridge/backend together. It also checks negative source
contracts for invalid offsets, forbidden regressions, disabled or missing routes, FIFO boundaries,
clamping, error paths and explicitly unsupported behavior. These checks prove
that the guards and limitations remain in the source; they do not claim to have
injected invalid peripheral transactions at runtime.

Component contracts verify electrical models, property integration, protocol
encoding and UI state transitions directly in their implementation sources.
MCU and direction filters limit the ESP matrix; component contracts still run.

Before the contracts run, the runner executes self-tests for malformed JSON,
invalid schemas, unsafe paths, ordered guard matching and invalid CLI filters.
The IDE layer then boots bundled firmware through the real SimulIDE-QEMU process
boundary. A failure is reported with its MCU, direction, scenario and source
path. Every passing contract reports separate positive and negative counts.

Bluetooth also runs the C++ executable under `tests/qemubt-runtime/`. It
constructs two production `QemuBt` controllers on separate packet arenas and
checks connection establishment, ACL credit backpressure, RX-ring retry, and
peer teardown without duplicating the controller in Python.

The BLE e2e gate builds stock ESP-IDF NimBLE peripheral and central fixtures,
runs both through the real SimulIDE-QEMU boundary, and requires a complete GATT
subscribe/write/notify/read round trip. IDF 4.4.7 and ESP32 are the defaults.
Use `BLE_IDF_VERSION=5.5.5` or `6.1` and `BLE_IDF_TARGET=esp32-s3` or
`esp32-c3` to select one pair. The value `all` selects every value on one axis;
`BLE_E2E_MATRIX=1` runs all nine IDF/MCU pairs. Every pair gets one smoke run;
the gate does not retry a failed process.

Normal IDE builds run this suite automatically. For an intentional debug build
without tests, use:

```sh
SIMULIDE_SKIP_TESTS=1 make
```
