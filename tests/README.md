# ESP emulation tests

The suite mirrors the documented implementation areas for every supported ESP
family. Emulator contracts live in `tests/<mcu>/<direction>/test.json`; IDE
end-to-end boot tests live in `tests/ide/`.

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
contracts for invalid offsets, disabled or missing routes, FIFO boundaries,
clamping, error paths and explicitly unsupported behavior. These checks prove
that the guards and limitations remain in the source; they do not claim to have
injected invalid peripheral transactions at runtime.

Before the contracts run, the runner executes self-tests for malformed JSON,
invalid schemas, unsafe paths, ordered guard matching and invalid CLI filters.
The IDE layer then boots bundled firmware through the real SimulIDE-QEMU process
boundary. A failure is reported with its MCU, direction, scenario and source
path. Every passing contract reports separate positive and negative counts.

Normal IDE builds run this suite automatically. For an intentional debug build
without tests, use:

```sh
SIMULIDE_SKIP_TESTS=1 make
```
