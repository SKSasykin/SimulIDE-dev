# Поддержка ESP I2C и SPI

Эта матрица сравнивает устройства ESP, доступные в SimulIDE, с контроллерами, описанными Espressif. Контроллеры SPI памяти, зарезервированные под flash и PSRAM, исключены из количества универсальных SPI.

| Устройство | Документированный I2C | Документированный универсальный SPI | Реализация SimulIDE |
| --- | --- | --- | --- |
| ESP8266EX | Программная реализация через GPIO, до 100 кГц; аппаратного контроллера I2C нет | 1 HSPI, master до 80 МГц и slave до 20 МГц; фиксированные GPIO15 CS, GPIO14 CLK, GPIO12 MISO, GPIO13 MOSI | Битовый I2C через GPIO; опрашиваемые передачи HSPI master через фиксированные пины |
| ESP32 | 2 контроллера, master/slave, 100/400 кбит/с и программируемо до 5 МГц; GPIO Matrix | SPI2/HSPI и SPI3/VSPI, master до 80 МГц; IO_MUX или GPIO Matrix | 2 опрашиваемых I2C master с FIFO/списком команд и 2 буферизованных опрашиваемых SPI master; маршрутизация IO_MUX/matrix |
| ESP32-S3 | 2 контроллера, master/slave, 100/400 и до 800 кбит/с; GPIO Matrix | SPI2/FSPI и SPI3, master до 80 МГц и slave до 60 МГц; SPI2 IO_MUX/matrix, SPI3 только matrix | 2 опрашиваемых I2C master с FIFO/списком команд и 2 буферизованных опрашиваемых SPI master; прямые пины SPI2 и маршрутизация GPIO Matrix |
| ESP32-C3 | 1 контроллер, master/slave, 100/400 и до 800 кбит/с; GPIO Matrix | SPI2/FSPI, master до 80 МГц и slave до 60 МГц; IO_MUX или matrix | 1 опрашиваемый I2C master с FIFO/списком команд и 1 буферизованный опрашиваемый SPI master; прямые пины SPI2 и маршрутизация GPIO Matrix |

## Реализованный объём

- I2C: доступ CPU к TX/RX FIFO, списки команд RSTART/WRITE/READ/STOP/END, флаги DONE для каждой команды, обработка ACK/NACK, тайминги тактирования контроллера, опрашиваемые регистры статуса прерываний и доставка прерываний гостю через мост SimulIDE-to-QEMU. Также мостятся APB-алиасы TX FIFO классического ESP32, используемые ESP-IDF. Поддерживаются раскладки как классического ESP32 (16 слотов команд), так и современных ESP32-S3/C3 (8 слотов команд, другая кодировка opcode).
- SPI: буферы данных CPU до 64 байт, обработка запуска `CMD.USR` с самоочисткой современного `CMD.UPDATE`, программируемая длина данных, делитель тактов, CPOL/CPHA, порядок бит, автоматический CS0 и флаги завершения для опроса.
- ESP32-S3 и ESP32-C3 используют современные раскладки периферии и смещения регистров GPIO Matrix. Маршрутизация пинов I2C и SPI работает через GPIO Matrix, а также через прямые IO_MUX-пины там, где они есть в кремнии (ESP32 HSPI/VSPI, ESP32-S3/C3 SPI2, ESP8266 HSPI).
- ESP8266 не предоставляет вымышленный аппаратный контроллер I2C; программный I2C по-прежнему использует GPIO.
- App-only образы PlatformIO для классического ESP32 обнаруживаются и автоматически объединяются с соседними `bootloader.bin` и `partitions.bin` в образ 4 МБ. Принимаются точные образы 2/4/8/16 МБ, а меньшие корректные бинарники дополняются до 4 МБ.

Пока не реализовано: slave-режим для I2C и SPI, DMA, доставка гостевых прерываний SPI, фазы command/address/dummy SPI, передачи dual/quad/octal, арбитраж и полное поведение таймингов/ошибок. Текущая реализация ориентирована на master-драйверы в режиме опроса.

## Официальные источники

- [ESP8266EX Datasheet v7.1](https://www.espressif.com/sites/default/files/documentation/0a-esp8266ex_datasheet_en.pdf)
- [ESP8266 Technical Reference v1.7](https://www.espressif.com/sites/default/files/documentation/esp8266-technical_reference_en.pdf)
- [ESP32 Series Datasheet v5.3](https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf)
- [ESP32 Technical Reference Manual v5.8](https://www.espressif.com/sites/default/files/documentation/esp32_technical_reference_manual_en.pdf)
- [ESP32-S3 Series Datasheet v2.2](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf)
- [ESP32-S3 Technical Reference Manual v1.8](https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf)
- [ESP32-C3 Series Datasheet v2.4](https://www.espressif.com/sites/default/files/documentation/esp32-c3_datasheet_en.pdf)
- [ESP32-C3 Technical Reference Manual v1.4](https://www.espressif.com/sites/default/files/documentation/esp32-c3_technical_reference_manual_en.pdf)
