# SimulIDE BLE GATT Demo Firmware

The bundled firmware is built with ESP-IDF 5.5.5 from the pinned Docker image:

```text
espressif/idf@sha256:a9231d0697ab8f7517cc072e93b7c83e04907bfbfba80b6440d7dbbf90665cf2
```

From the repository root, build and install all six images with:

```sh
python3 resources/data/bin/esp/examples/ble-gatt/build.py
```

The script builds out of tree under `tmp/ble-gatt-release/`, merges images from
ESP-IDF's generated `flasher_args.json`, installs only final 2 MiB images under
`resources/data/bin/{esp32,esp32s3,esp32c3}/`, and removes the temporary build tree.

## Release SHA-256

The hashes below identify the currently bundled images and are verified by the
source-contract test:

```text
e478615a0d7ed5593aebcc113aa749e38fc29b84cf667649fe53dc5cb903bfcc  resources/data/bin/esp32/ble_gatt_peripheral.merged.bin
fa871046afe256510e5a0e7ad3e4b9c09714a25d0e1e1d1a92a386aff72ad2eb  resources/data/bin/esp32/ble_gatt_central.merged.bin
358876968c2e254e88e3463c846fb09096f292e1b7776de414f8692d0584e71d  resources/data/bin/esp32s3/ble_gatt_peripheral.merged.bin
2e6660d87636ca5ebb15b15ec2250998c01bd21f4155c3ce047f6c69688ec887  resources/data/bin/esp32s3/ble_gatt_central.merged.bin
93fbec607e05e98985cdab7a1ea551615f99b122a8777480999868aaaa7cab01  resources/data/bin/esp32c3/ble_gatt_peripheral.merged.bin
1b55d1716984163ccb41439f885540f061a94c95c51e6781c94cab8dcea05928  resources/data/bin/esp32c3/ble_gatt_central.merged.bin
```
