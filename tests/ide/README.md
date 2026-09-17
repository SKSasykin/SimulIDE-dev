# SimulIDE integration tests

`run-smoke-tests.sh` locates the newest built SimulIDE executable unless one is
passed explicitly. It boots the bundled Blink firmware for all four ESP
families and verifies the virtual-WiFi HTTP response for ESP32, ESP32-S3 and
ESP32-C3. It also runs each bundled two-device BLE GATT Demo and requires a
completed subscribe/write/notify/read round trip.

These tests cross the real IDE, shared-memory bridge and QEMU process boundary.
ESP8266 WiFi remains contract-tested only because no compatible bundled guest
demo exists yet. BLE controller-to-controller behavior is covered separately by
`tests/qemubt-runtime/`. The main test runner also has a build-from-source
ESP-IDF/NimBLE GATT matrix across three IDF versions and three MCU families;
the IDE smoke layer validates the bundled ESP-IDF 5.5.5 user examples.
