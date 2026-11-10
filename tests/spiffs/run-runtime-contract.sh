#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
QEMU_CPP="$ROOT_DIR/src/microsim/cores/qemu/qemudevice.cpp"
SPIFFS_DIALOG_CPP="$ROOT_DIR/src/gui/spiffs/spiffsdialog.cpp"
ESP32_CPP="$ROOT_DIR/src/microsim/cores/qemu/esp32/esp32.cpp"
ESP32S3_CPP="$ROOT_DIR/src/microsim/cores/qemu/esp32/esp32s3.cpp"
ESP32C3_CPP="$ROOT_DIR/src/microsim/cores/qemu/esp32/esp32c3.cpp"
ESP8266_CPP="$ROOT_DIR/src/microsim/cores/qemu/esp8266/esp8266.cpp"

grep -F 'return {};' "$QEMU_CPP" >/dev/null
grep -F 'QFile::copy( source, dest )' "$QEMU_CPP" >/dev/null
grep -F 'return source;' "$QEMU_CPP" >/dev/null
grep -A2 -F 'QFile::copy( source, dest )' "$QEMU_CPP" | grep -F 'return {};' >/dev/null
if grep -A12 -F 'void SpiffsDialog::saveCurrent()' "$SPIFFS_DIALOG_CPP" | grep -F 'path = m_device->firmware()' >/dev/null; then
    exit 1
fi
grep -F 'QString SpiffsDialog::writablePath() const' "$SPIFFS_DIALOG_CPP" >/dev/null
grep -F 'return m_device->flashPath();' "$SPIFFS_DIALOG_CPP" >/dev/null
grep -F 'm_saveButton->setEnabled( !m_currentPath.isEmpty()' "$SPIFFS_DIALOG_CPP" >/dev/null
grep -F 'm_flashPath = writableFlashCopy' "$ESP32_CPP" >/dev/null
grep -F 'm_flashPath = writableFlashCopy' "$ESP32S3_CPP" >/dev/null
grep -F 'm_flashPath = writableFlashCopy' "$ESP32C3_CPP" >/dev/null
grep -F 'm_flashPath = writableFlashCopy' "$ESP8266_CPP" >/dev/null
printf '%s\n' 'Writable flash runtime contract PASS'
