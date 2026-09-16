#!/usr/bin/env bash
set -eu

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"

# Clean up test artifacts from resources/data (should not be there after test)
rm -f "$ROOT_DIR/resources/data/bin/esp32/ble_gatt_peripheral_test.merged.bin"

# Clean up tmp build directory
rm -rf "$ROOT_DIR/tmp/ble-gatt-e2e-test"

echo "Cleanup complete"