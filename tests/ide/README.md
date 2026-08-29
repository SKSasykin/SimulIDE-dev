# SimulIDE integration tests

`run-smoke-tests.sh` locates the newest built SimulIDE executable unless one is
passed explicitly. It boots the bundled Blink firmware for all four ESP
families and verifies the virtual-WiFi HTTP response for ESP32, ESP32-S3 and
ESP32-C3.

These tests cross the real IDE, shared-memory bridge and QEMU process boundary.
ESP8266 WiFi remains contract-tested only because no compatible bundled guest
demo exists yet.
