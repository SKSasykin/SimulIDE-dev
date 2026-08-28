# Поддержка АЦП ESP

Эта матрица описывает входы SAR АЦП устройств ESP, доступных в SimulIDE: каналы, их соответствие GPIO-пинам, эффективный диапазон измерения (полная шкала) для каждого кода ослабления и разрядность результата. Все значения взяты из официальных datasheet Espressif.

| Устройство | Блок АЦП | Каналы | Соответствие каналов GPIO | Полная шкала ослабления (В), коды 0-3 | Разрядность |
| --- | --- | --- | --- | --- | --- |
| ESP8266EX | один SAR | 1 (TOUT) | A0/TOUT | фикс. 0-1 В (без ослабления) | 10 бит, SAR с 8 выборками |
| ESP32 | ADC1 | 8 (CH0-7) | GPIO36, GPIO37, GPIO38, GPIO39, GPIO32, GPIO33, GPIO34, GPIO35 | 0.95, 1.25, 1.75, 2.45 | программируемая 9/10/11/12 бит |
| ESP32 | ADC2 | 10 (CH0-9) | GPIO4, GPIO0, GPIO2, GPIO15, GPIO13, GPIO12, GPIO14, GPIO27, GPIO25, GPIO26 | 0.95, 1.25, 1.75, 2.45 | программируемая 9/10/11/12 бит |
| ESP32-S3 | ADC1 | 10 (CH0-9) | GPIO1 .. GPIO10 | 0.85, 1.10, 1.60, 2.90 | фикс. 12 бит |
| ESP32-S3 | ADC2 | 10 (CH0-9) | GPIO11 .. GPIO20 | 0.85, 1.10, 1.60, 2.90 | фикс. 12 бит |
| ESP32-C3 | ADC1 | 5 (CH0-4) | GPIO0 .. GPIO4 | 0.75, 1.05, 1.30, 2.50 | фикс. 12 бит |
| ESP32-C3 | ADC2 | 1 (CH0) | GPIO5 | 0.75, 1.05, 1.30, 2.50 | фикс. 12 бит |

## Реализованный объём

- Каждый доступ к SAR АЦП передаётся из QEMU в SimulIDE через мост SimulIDE-to-QEMU, который устанавливает узкое MMIO-окно ровно над блоком регистров АЦП каждого чипа:
- ESP32: блок SENS по адресу `0x3FF48800`, размер 0x400 байт.
- ESP32-S3: блок SENS по адресу `0x60008800`, размер 0x200 байт.
- ESP32-C3: блок APB_SARADC по адресу `0x60040000`.
- ESP8266: блок SAR по адресу `0x60000D00`, размер 0x100 байт.
- ESP32 / ESP32-S3: смоделирован классический поток с двумя блоками АЦП. Запись `meas_start` с `sar1_start_sar` / `sar2_start_sar`, выполняемая опрашивающим драйвером, преобразует включённый канал (выбранный полем `sarN_en_pad`) и публикует сырое значение вместе с `measN_done_sar` обратно в регистр запуска, поэтому `adc_ll_rtc_convert_is_done()` и `adc_ll_rtc_get_convert_value()` видят ожидаемые данные.
- Поиск соответствия канала GPIO использует таблицу АЦП из datasheet, поэтому напряжение GPIO-пина, заданное аналоговой схемой, считывается и масштабируется к выбранному диапазону ослабления и разрядности.
- Ширина результата классического ESP32 программируется через регистр блока SENS со смещением `0x2C` (`SENS_SAR_START_FORCE_REG`): ADC1 использует биты 1:0, ADC2 использует биты 3:2, коды 0-3 выбирают 9, 10, 11 или 12 бит. Значение после сброса по умолчанию — 12 бит. ESP32-S3 и ESP32-C3 выдают фиксированное 12-битное значение.
- ESP32-S3 и ESP32-C3 используют свои современные раскладки регистров периферии (`sar_meas1_ctrl2`/`sar_meas2_ctrl2` для S3, `one_time_sample` плюс `data1_status`/`data2_status`/`int_raw` для C3).
- ESP8266 предоставляет один внешний вход SAR `A0/TOUT`. Напряжение на пине масштабируется к входному диапазону 0-1 В и кодируется в 10-битное значение. Запуск преобразования (`SAR_START`) записывает восемь слотов результата SAR в совместимой с SDK кодировке, которую ожидают `analogRead()`/`__analogRead()` из SDK, поэтому существующие прошивки продолжают работать без изменений.
- Корпус ESP8266 теперь предоставляет пин `A0/TOUT` (стабильный идентификатор `A0`) вместе с существующими GPIO-пинами.

## Известные ограничения

- Аппаратная ошибка ESP32-C3 ADC2_CH0 не моделируется.
- Конкуренция между ESP32 ADC2 и Wi-Fi не моделируется.
- Внутренний путь измерения VDD у ESP8266 не моделируется.

## Официальные источники

- [ESP8266EX Datasheet v7.1](https://www.espressif.com/sites/default/files/documentation/0a-esp8266ex_datasheet_en.pdf)
- [ESP8266 Technical Reference v1.7](https://www.espressif.com/sites/default/files/documentation/esp8266-technical_reference_en.pdf)
- [ESP32 Series Datasheet v5.3](https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf)
- [ESP32 Technical Reference Manual v5.8](https://www.espressif.com/sites/default/files/documentation/esp32_technical_reference_manual_en.pdf)
- [ESP32-S3 Series Datasheet v2.2](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf)
- [ESP32-S3 Technical Reference Manual v1.8](https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf)
- [ESP32-C3 Series Datasheet v2.4](https://www.espressif.com/sites/default/files/documentation/esp32-c3_datasheet_en.pdf)
- [ESP32-C3 Technical Reference Manual v1.4](https://www.espressif.com/sites/default/files/documentation/esp32-c3_technical_reference_manual_en.pdf)
