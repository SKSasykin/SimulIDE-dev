#!/usr/bin/env bash
set -eu

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="$ROOT_DIR/tmp/ble-gatt-e2e-test"
FIXTURE_DIR="$ROOT_DIR/tests/fixtures/ble-gatt-e2e"

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

# Build peripheral firmware in tmp
docker run --rm --platform linux/arm64 \
  -v "$ROOT_DIR:/project" \
  -w "/project/$FIXTURE_DIR/peripheral" \
  "espressif/idf@sha256:52bc81e7f212b6cc63b31ea57b8270badb3236e44df8567a57e2d1a6c74c5000" \
  idf.py build

# Create merged binary in tmp
docker run --rm --platform linux/arm64 \
  -v "$ROOT_DIR:/project" \
  -w "/project/$FIXTURE_DIR/peripheral" \
  "espressif/idf@sha256:52bc81e7f212b6cc63b31ea57b8270badb3236e44df8567a57e2d1a6c74c5000" \
  esptool.py --chip esp32 merge_bin -o "$BUILD_DIR/ble_gatt_peripheral.merged.bin" \
  --flash_mode dio --flash_freq 40m --flash_size 2MB --fill-flash-size 2MB \
  0x1000 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0x10000 build/ble_gatt_peripheral.bin

# Copy merged binary to where SimulIDE expects it (for testing)
cp "$BUILD_DIR/ble_gatt_peripheral.merged.bin" "$ROOT_DIR/resources/data/bin/esp32/ble_gatt_peripheral_test.merged.bin"

# Copy circuit
cp "$FIXTURE_DIR/circuits/esp32-ble-gatt-peripheral.sim2" "$BUILD_DIR/"

echo "Build complete. Artifacts in $BUILD_DIR"
echo "Run smoke test with:"
echo "  HOME=\"$ROOT_DIR/tmp\" SIMULIDE_TEST_MODE=1 QT_QPA_PLATFORM=offscreen \\"
echo "  \"$ROOT_DIR/build/executables/simulide-*.app/Contents/MacOS/simulide-*\" \\"
echo "  -silent -nogui -smoke-test \"$BUILD_DIR/esp32-ble-gatt-peripheral.sim2\" 15000"