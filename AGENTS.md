# Agent Rules & Constraints for SimulIDE-dev

## Session-Level Rules (Must Persist)

### 1. Working Directory & Temp Files
- **All temporary files and build artifacts go in `./tmp/`** — never in `/tmp`, `$TMPDIR`, or system temp
- `./tmp/` is the only allowed location for test builds, runtime harnesses, smoke test configs
- `build/tests/` must never exist — removed if created by qmake

### 2. User Examples & Resources (Strict)
- **Never add artifacts to `resources/data/bin/esp/examples/`** — user-facing examples only
- **Never add circuits to `resources/data/examples/`** — user-facing examples only
- **Never add binaries to `resources/data/bin/esp32/`, `esp32s3/`, `esp32c3/`, `esp8266/`** unless they are official user examples
- Test artifacts (firmware, circuits, merged bins) go in `./tmp/` or `tests/` only
- If a test binary must be loaded by SimulIDE, build it in `./tmp/` and reference from there

### 3. Build & Bundle Size
- `SimulIDE.pri` copies entire `resources/data/` into `.app` bundle
- **Never leave test binaries in `resources/data/bin/`** — they bloat the app
- Verify bundle size after each build (`du -sh build/executables/*.app`)
- Target size: ~101 MB for macOS arm64

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
- **No GATT/ATT in controller** — stays in NimBLE host
- **No controller-side encryption/SMP** — not implemented

### 7. ESP-IDF Firmware Builds
- Use pinned Docker image: `espressif/idf@sha256:52bc81e7f212b6cc63b31ea57b8270badb3236e44df8567a57e2d1a6c74c5000` (v4.4.7)
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
./resources/data/bin/esp/examples/  ← USER EXAMPLES ONLY (blink, wifi-http, common)
./resources/data/examples/      ← USER CIRCUITS ONLY
```

---

## Forbidden Patterns
- ❌ `mkdir -p /tmp/...` or `TMPDIR=/tmp``
- ❌ `build/tests/` directory
- ❌ Commits without user request
- ❌ Modifying unrelated user files (OLED, etc.)