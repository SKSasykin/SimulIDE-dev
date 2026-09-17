# Agent Rules & Constraints for SimulIDE-dev

## Session-Level Rules (Must Persist)

### 1. Working Directory & Temp Files
- **All temporary files and build artifacts go in `./tmp/`** — never in `/tmp`, `$TMPDIR`, or system temp
- `./tmp/` is the only allowed location for test builds, runtime harnesses, smoke test configs
- `build/tests/` must never exist — removed if created by qmake

### 2. User Examples & Resources (Strict)
- Official ESP firmware sources live in `resources/data/bin/esp/examples/`
- Official merged firmware lives directly in the established MCU directories:
  - ESP32: `resources/data/bin/esp32/`
  - ESP32-S3: `resources/data/bin/esp32s3/`
  - ESP32-C3: `resources/data/bin/esp32c3/`
- Never create target firmware directories under `resources/data/bin/esp/`; that
  directory is reserved for shared ESP sources and ROM data
- All ESP boot ROM resources live in `resources/data/bin/esp/rom/bin/`:
  tracked ESP8266 `*.rom` files and generated ESP32-family `*.bin` files
- Official circuits live in `resources/data/examples/<controller-type>/`; use
  `resources/data/examples/common/` only for a circuit shared unchanged by multiple controllers
- A controller-specific example must have its circuit in that controller's examples directory
- **Never add test artifacts to these user-facing example directories**
- Test artifacts (firmware, circuits, merged bins) go in `./tmp/` or `tests/` only
- If a test binary must be loaded by SimulIDE, build it in `./tmp/` and reference from there
- Build official firmware out of tree under `./tmp/`, then copy only the final merged
  image into the matching direct MCU directory listed above
- If any tool creates `build/`, `sdkconfig`, dependency caches or other generated files
  inside `resources/data/bin/esp/examples/`, move the final firmware to its target
  directory and remove every generated build artifact from the source tree

### 3. Build & Bundle Size
- `SimulIDE.pri` copies entire `resources/data/` into `.app` bundle
- **Never leave test binaries in `resources/data/bin/`** — they bloat the app
- `make` runs `scripts/build_qemu.sh` first; QEMU builds only in
  `build/qemu-simulide/`, then installs its binaries and ROMs into
  `resources/data/bin/`. The subsequent SimulIDE build copies those fresh
  resources into the new `.app`; the QEMU script must never modify an existing `.app`
- A correct fresh macOS build must regenerate the Makefile before `make`, otherwise
  an existing timestamped `.app` can be relinked instead of creating a new build:
  ```sh
  /opt/homebrew/bin/qmake -o build/Makefile build/SimulIDE_Build.pro
  make -C build -j4
  ```
- For an intentional build without tests, use
  `SIMULIDE_SKIP_TESTS=1 make -C build -j4` after the same `qmake` command
- To launch the newest build, run `./start.sh` from the repository root
- Verify bundle size after each build (`du -sh build/executables/*.app`)
- Target size: ~113 MB for macOS arm64 with the bundled BLE GATT firmware matrix

### 4. Testing
- Contract tests: `./tests/run-tests.sh --contracts-only`
- Full tests: `./tests/run-tests.sh`
- Smoke tests: `./tests/ide/run-smoke-tests.sh` (uses `./tmp/` for HOME)
- Runtime harness: `./tests/qemubt-runtime/run.sh` (builds in `./tmp/qemubt-runtime/`)
- All test working directories must be under `./tmp/`
- Test firmware/examples/circuits: source in `./tests/fixtures/`, builds in `./tmp/`, cleaned after
- Never leave build artifacts in `./tmp/` after test completion

### 5. Commits & Git
- **No commits without explicit user command** — user said: "больше не коммить без соответствующей команды"
- Local commits only, no push unless asked
- Keep unrelated user changes (e.g., `src/components/outputs/displays/oledcontroller.cpp/.h`) unstaged and untouched

### 6. BLE Implementation Scope
- Phase 1: HCI Reset transport (QEMU `esp32-ble-hci` + SimulIDE `QemuBt`)
- Phase 2: Full NimBLE startup command set (Version, Features, Event Mask, BD_ADDR, Privacy, Flow Control)
- Phase 3: Legacy advertising + passive/active scanning + deterministic in-process BLE medium
- Phase 4: Connections + ACL (Create/Cancel/Disconnect/Remote Features, PB translation, credits)
- Phase 5: Runtime harness (`tests/qemubt-runtime/`) + VHCI wrapper for ESP-IDF firmware
- Phase 6: Multi-version and multi-MCU NimBLE support — IDF 4.4.x, 5.x LTS,
  and 6.x LTS across ESP32, ESP32-S3, and ESP32-C3 (done)
- **Multi-IDF requirement**: user examples on IDF 4, 5 and 6 must work correctly
- **Multi-version strategy**: one `QemuBt` answering the superset of startup commands;
  4.4.7 behavior must stay green as regression baseline
- QEMU transport (`esp32_ble_hci.c`) is version-agnostic — IDF upgrades touch
  `QemuBt` command table, tests and possibly the VHCI shim, not QEMU
- **No GATT/ATT in controller** — stays in NimBLE host
- **No controller-side encryption/SMP** — not implemented

### 7. ESP-IDF Firmware Builds
- Pinned Docker images (multi-IDF BLE requirement):
  - v4.4.7: `espressif/idf@sha256:52bc81e7f212b6cc63b31ea57b8270badb3236e44df8567a57e2d1a6c74c5000`
  - v5.5.5: `espressif/idf@sha256:a9231d0697ab8f7517cc072e93b7c83e04907bfbfba80b6440d7dbbf90665cf2`
  - v6.1: `espressif/idf@sha256:81893c71bb5e570088901f21def8684c25cd2a9020281bd01b843a7655edb18c`
- e2e runner selects image via `BLE_IDF_VERSION` (4.4.7 default) and target
  via `BLE_IDF_TARGET` (ESP32 default); `BLE_E2E_MATRIX=1` runs all nine pairs
- Build in `./tmp/` or project example dirs, never pollute `resources/data/`
- Merged binaries via `esptool.py merge_bin --fill-flash-size 2MB`

### 8. Code Style
- No comments unless asked
- C++11 (gnu++11), Qt5
- Follow existing patterns in `src/microsim/cores/qemu/`

### 9. Documentation
- Update `README.md`, `README.ru.md`, `docs/esp-wireless-support.md`, `docs/esp-wireless-support.ru.md` after each phase
- Update `third_party/qemu-simulide/README.rst` for QEMU-side changes

### 10. Memory (Engram)
- Save observations after significant work: `mem_save`
- Save session summary at end: `mem_session_summary`
- Tag observations with `topic_key` for upserts (e.g., `architecture/esp-ble-phaseN`)

---

## Quick Reference: Key Paths
```
./tmp/                          ← ALL temp artifacts
./build/                        ← Main build output (objects, executables, qemu-simulide)
./tests/qemubt-runtime/         ← Runtime harness source (builds in ./tmp/)
./tests/ide/run-smoke-tests.sh  ← Smoke tests (uses ./tmp/ for HOME)
./resources/data/bin/esp/examples/  ← OFFICIAL FIRMWARE SOURCES ONLY
./resources/data/bin/esp/rom/bin/   ← SHARED ESP BOOT ROM RESOURCES
./resources/data/bin/esp32/         ← ESP32 MERGED FIRMWARE
./resources/data/bin/esp32s3/       ← ESP32-S3 MERGED FIRMWARE
./resources/data/bin/esp32c3/       ← ESP32-C3 MERGED FIRMWARE
./resources/data/examples/<type>/   ← CONTROLLER-SPECIFIC USER CIRCUITS
./resources/data/examples/common/   ← CIRCUITS SHARED UNCHANGED BY CONTROLLERS
```

---

## Forbidden Patterns
- ❌ `mkdir -p /tmp/...` or `TMPDIR=/tmp``
- ❌ `build/tests/` directory
- ❌ Commits without user request
- ❌ Modifying unrelated user files (OLED, etc.)
