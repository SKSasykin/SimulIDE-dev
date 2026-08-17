# ESP I2C and SPI support

This matrix compares the ESP devices available in SimulIDE with the controllers documented by Espressif. Memory SPI controllers reserved for flash and PSRAM are excluded from the general-purpose SPI count.

| Device | Documented I2C | Documented general-purpose SPI | SimulIDE implementation |
| --- | --- | --- | --- |
| ESP8266EX | Software GPIO implementation, up to 100 kHz; no hardware I2C controller | 1 HSPI, master up to 80 MHz and slave up to 20 MHz; fixed GPIO15 CS, GPIO14 CLK, GPIO12 MISO, GPIO13 MOSI | GPIO bit-banged I2C; polling HSPI master transfers through the fixed pins |
| ESP32 | 2 controllers, master/slave, 100/400 kbit/s and programmable up to 5 MHz; GPIO Matrix | SPI2/HSPI and SPI3/VSPI, master up to 80 MHz; IO_MUX or GPIO Matrix | 2 FIFO/command-list polling I2C masters and 2 buffered polling SPI masters; IO_MUX/matrix routing |
| ESP32-S3 | 2 controllers, master/slave, 100/400 and up to 800 kbit/s; GPIO Matrix | SPI2/FSPI and SPI3, master up to 80 MHz and slave up to 60 MHz; SPI2 IO_MUX/matrix, SPI3 matrix only | 2 FIFO/command-list polling I2C masters and 2 buffered polling SPI masters; direct SPI2 pins and GPIO Matrix routing |
| ESP32-C3 | 1 controller, master/slave, 100/400 and up to 800 kbit/s; GPIO Matrix | SPI2/FSPI, master up to 80 MHz and slave up to 60 MHz; IO_MUX or matrix | 1 FIFO/command-list polling I2C master and 1 buffered polling SPI master; direct SPI2 pins and GPIO Matrix routing |

## Implemented scope

- I2C implements CPU FIFO access, RSTART/WRITE/READ/STOP/END command lists, ACK/NACK status, polling interrupt-status registers, and controller clock timing.
- SPI implements CPU data buffers up to 64 bytes, `CMD.USR`, programmable data length, clock divider, CPOL/CPHA, bit order, and automatic CS0.
- ESP32-S3 and ESP32-C3 use their modern peripheral layouts and GPIO Matrix register offsets.
- ESP8266 does not expose a fictitious hardware I2C controller; software I2C continues to use GPIO.

Not implemented yet: I2C or SPI slave operation, DMA, guest interrupt delivery, SPI command/address/dummy phases, dual/quad/octal transfers, arbitration, and complete timing/error behavior. The current implementation targets polling-mode master drivers.

## Official references

- [ESP8266EX Datasheet v7.1](https://www.espressif.com/sites/default/files/documentation/0a-esp8266ex_datasheet_en.pdf)
- [ESP8266 Technical Reference v1.7](https://www.espressif.com/sites/default/files/documentation/esp8266-technical_reference_en.pdf)
- [ESP32 Series Datasheet v5.3](https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf)
- [ESP32 Technical Reference Manual v5.8](https://www.espressif.com/sites/default/files/documentation/esp32_technical_reference_manual_en.pdf)
- [ESP32-S3 Series Datasheet v2.2](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf)
- [ESP32-S3 Technical Reference Manual v1.8](https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf)
- [ESP32-C3 Series Datasheet v2.4](https://www.espressif.com/sites/default/files/documentation/esp32-c3_datasheet_en.pdf)
- [ESP32-C3 Technical Reference Manual v1.4](https://www.espressif.com/sites/default/files/documentation/esp32-c3_technical_reference_manual_en.pdf)
