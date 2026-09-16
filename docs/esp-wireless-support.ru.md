# Поддержка виртуального WiFi и Bluetooth ESP

Этот документ описывает средства беспроводной сети, предоставляемые устройствами ESP в SimulIDE. Виртуальный WiFi — это интеграция на уровне пакетов между гостевой прошивкой, форком QEMU и libslirp. Это не симуляция RF или 802.11 MAC/PHY. Для разработки реализованы BLE HCI-транспорт, профиль команд запуска NimBLE, legacy undirected advertising/scanning, детерминированные соединения и непрозрачная пересылка ACL, но Bluetooth сейчас не является поддерживаемой пользовательской функцией.

## Матрица поддержки

| Устройство | Backend виртуального WiFi | Встроенный HTTP-демо | Bluetooth в кремнии | Bluetooth в SimulIDE |
| --- | --- | --- | --- | --- |
| ESP32 | SLC DMA NIC с DHCP/NAT libslirp | Да | Classic + BLE | HCI-транспорт, детерминированные LE-соединения и непрозрачная пересылка ACL |
| ESP32-S3 | SLC DMA NIC с DHCP/NAT libslirp | Да | BLE | HCI-транспорт, детерминированные LE-соединения и непрозрачная пересылка ACL |
| ESP32-C3 | SLC DMA NIC с DHCP/NAT libslirp | Да | BLE | HCI-транспорт, детерминированные LE-соединения и непрозрачная пересылка ACL |
| ESP8266EX | Доступен виртуальный SLC NIC | Гостевого демо пока нет | Нет | Неприменимо |

## Архитектура виртуального WiFi

Рабочий путь пакета:

```text
Приложение ESP
  -> пользовательский драйвер ESP-NETIF
  -> дескрипторы ESP SLC DMA
  -> виртуальный NIC QEMU esp32.slc
  -> backend QEMU libslirp
  -> сеть хоста
```

Обратный путь проходит те же уровни в противоположном направлении. Гость получает обычные Ethernet-кадры и использует свой штатный стек lwIP для ARP, DHCP, IP, TCP и UDP. libslirp предоставляет приватную пользовательскую сеть, DHCP и NAT; TAP-интерфейс и права администратора не требуются.

Модель QEMU SLC копирует TX и RX кадры через кольца гостевых DMA-дескрипторов. RX из backend ставится в очередь и доставляется асинхронно по таймеру host-clock, чтобы ответы не гонялись с инициализацией ESP-NETIF link у гостя. Арена разделяемой памяти SimulIDE также содержит кольца WiFi-пакетов для будущих simulator-to-simulator связей, но сейчас поддерживаемый Internet backend — libslirp.

## Требования к прошивке

Встроенные демо используют пользовательский транспорт ESP-NETIF, который представляет эмулируемый SLC-контроллер как Ethernet-подобный интерфейс. Чтобы использовать виртуальный WiFi, прошивка должна включать этот транспорт.

Реализация не эмулирует 802.11-радио, точки доступа, каналы, association, WPA или закрытый аппаратный WiFi-контроллер Espressif. Поэтому произвольный штатный бинарник, использующий `WiFi.begin()` или нативный WiFi-драйвер Espressif, не получит сеть автоматически только потому, что NIC QEMU присутствует.

Встроенные end-to-end примеры:

- `resources/data/examples/esp32/esp32 WiFi HTTP Hello World.sim2`
- `resources/data/examples/esp32-s3/esp32-s3 WiFi HTTP Hello World.sim2`
- `resources/data/examples/esp32-c3/esp32-c3 WiFi HTTP Hello World.sim2`

Их объединённые образы прошивок находятся в соответствующем каталоге устройства внутри `resources/data/bin/`.

## Проброс портов хоста

`HostForwardPort` управляет доступом с хоста к HTTP-серверу в госте:

- `0` отключает проброс и является значением по умолчанию.
- Положительное значение пробрасывает этот TCP-порт на всех IPv4-интерфейсах хоста на TCP-порт 80 гостя. Поэтому он также доступен через `127.0.0.1`.
- Встроенные HTTP-примеры задают `HostForwardPort="8080"` и доступны по адресу `http://127.0.0.1:8080/` после получения гостем DHCP-адреса.
- Выбранный порт хоста должен быть свободен. Несколько симуляций должны использовать разные порты проброса.

Так как текущее правило проброса привязывается ко всем IPv4-интерфейсам хоста, не открывайте недоверенные гостевые сервисы при подключении к недоверенной сети. В будущем свойство адреса хоста может позволить явно ограничить listener loopback-интерфейсом.

Обычные ESP-примеры оставляют проброс выключенным, не резервируют порт 8080 и могут запускаться параллельно. Исходящие соединения гостя используют NAT libslirp независимо от `HostForwardPort`.

## Требования к сборке

Виртуальный WiFi требует libslirp 4.0 или новее при сборке форка QEMU. Интеграция хоста SimulIDE также требует Qt5Network. `scripts/build_qemu.sh` проверяет наличие libslirp и настраивает QEMU с включённым SLIRP.

## Статус Bluetooth

Bluetooth Classic и BLE не реализованы end-to-end. В текущем дереве есть:

- кольца `bt_tx` и `bt_rx` в арене разделяемой памяти SimulIDE/QEMU;
- descriptor-based H4-транспорт по адресу `0x3ff52000` на ESP32 и `0x60012000` на ESP32-S3/C3 с level IRQ и асинхронной доставкой RX;
- контроллер `QemuBt`, реализующий полный набор HCI-команд для синхронизации хоста NimBLE в ESP-IDF 4.4.7: Reset, Read Local Version Info, Read Local Supported Features, Set Event Mask, Set Event Mask Page 2, LE Set Event Mask, LE Read Buffer Size, LE Read Local Supported Features, Read BD_ADDR, Host Buffer Size, Set Controller To Host Flow Control (ESP32), LE Set Address Resolution Enable, LE Clear Resolving List, LE Add Device To Resolving List, LE Set Privacy Mode (ESP32-S3/C3);
- legacy-команды undirected LE Set Advertising Parameters/Data/Scan Response/Enable и LE Set Scan Parameters/Enable со строгой проверкой параметров;
- внутрипроцессная детерминированная среда, общая для экземпляров `QemuBt`: включённый advertiser публикует снимок, пассивный scanner получает один LE Advertising Report, а активный — также Scan Response. Поддерживаются фильтрация дубликатов и backpressure RX-кольца;
- одно детерминированное LE-соединение на `QemuBt` с глобально уникальными локальными handle, legacy/enhanced Connection Complete, отменой, отключением и Remote Features Complete;
- непрозрачная пересылка H4 ACL между соединёнными контроллерами: ACL-фрагменты не разбираются, PB-флаги и локальные handle преобразуются, ограниченные очереди создают backpressure, а Number Of Completed Packets и настроенные host credits обеспечивают flow control;
- исходный код тестовой прошивки Reset в `resources/data/bin/esp/examples/ble-hci-reset/`.

Объект контроллера компилируется напрямую, а HCI framing проверяется точными byte-vector и source-contract тестами. Отдельный runtime C++ harness создаёт два production-экземпляра `QemuBt` с разными packet arenas и проверяет установление соединения, ACL credits, RX backpressure и удаление peer. End-to-end runtime-теста с гостевой NimBLE-прошивкой пока нет.

В текущем дереве нет:

- ISO data path, controller-side ATT/GATT, SMP и шифрования. Host-side L2CAP/ATT может работать поверх пересылаемых ACL-пакетов, но контроллер разработки не разбирает и не реализует эти протоколы;
- планирования интервалов, каналов, распространения сигнала, помех и коллизий: текущая среда работает по событиям активации, а не моделирует RF во времени;
- просмотра GATT или взаимодействия с ним в UI SimulIDE;
- проброса Bluetooth-адаптера через CoreBluetooth, BlueZ или WinRT.

По этой причине прежние экспериментальные настройки `WiFiLinkPort` и `BtLinkPort` не отображаются в панели свойств. Их нельзя трактовать как рабочую поддержку Bluetooth.

GATT-инспекция в UI SimulIDE, процедуры безопасности, временная RF-модель и проброс адаптера хоста остаются более поздними этапами.

## Важные файлы реализации

- `src/microsim/cores/qemu/qemudevice.{h,cpp}`: разделяемая арена и параметры запуска QEMU.
- `src/microsim/cores/qemu/qemuwifi.{h,cpp}`: модуль WiFi-колец SimulIDE.
- `src/microsim/cores/qemu/qemubt.{h,cpp}`: минимальный HCI-контроллер.
- `third_party/qemu-simulide/hw/misc/esp32_ble_hci.c`: гостевой DMA/H4-транспорт.
- `third_party/qemu-simulide/hw/dma/esp32_slc.c`: виртуальный NIC SLC DMA.
- `third_party/qemu-simulide/hw/misc/esp32-simulide-bridge.c`: карты моста разделяемой памяти.
- `third_party/qemu-simulide/net/slirp.c`: пользовательская сеть и проброс портов хоста.
