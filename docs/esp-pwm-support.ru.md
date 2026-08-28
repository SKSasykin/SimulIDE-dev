# Поддержка ESP PWM (LEDC)

Эта матрица описывает PWM-контроллеры LEDC устройств ESP, доступных в SimulIDE: каналы, группы таймеров, разрешение duty, ширину счётчика/DUTY, базовые адреса регистров и индексы выходных сигналов GPIO Matrix. Все значения взяты из официальных datasheet Espressif и заголовков ESP-IDF `soc`.

| Устройство | Модуль PWM | Каналы | Таймеры | Разрешение duty | Счётчик | Регистр duty | База регистров | Сигналы GPIO Matrix |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| ESP8266EX | нет (только программный PWM) | — | — | ~8-10 бит (soft) | — | — | — | — |
| ESP32 | LEDC | 16 (8 HS + 8 LS) | 8 (4 HS + 4 LS) | до 20 бит | 20 бит | 25 бит (21 цел + 4 дроб) | 0x3FF59000 | 71-86 |
| ESP32-S3 | LEDC | 8 (LS) | 4 | до 14 бит | 14 бит | 19 бит (15 цел + 4 дроб) | 0x60019000 | 73-80 |
| ESP32-C3 | LEDC | 6 (LS) | 4 | до 14 бит | 14 бит | 19 бит (15 цел + 4 дроб) | 0x60019000 | 45-50 |

Примечания по аппаратной части:

- Частота PWM равна `f_PWM = clk_src · 256 / (clock_divider · 2^duty_res)`, где `clock_divider` — 18-битное поле с фиксированной точкой и 8 дробными битами (коэффициент деления = field / 256). Битовое смещение поля в регистре отличается по чипам (ESP32 `S=5`, ESP32-S3/C3 `S=4`), но дробная часть всегда занимает 8 бит.
- `duty_res` выбирает полную шкалу счётчика `2^duty_res` (у ESP32 поле 5 бит и 20-битный счётчик; у ESP32-S3/C3 поле 4 бита и максимум 14 бит).
- Длительность высокого уровня в тиках задаётся целой частью регистра DUTY (`DUTY >> 4`); младшие 4 бита — дробная dithering-часть.
- Как и на реальном железе, каналы не привязаны к фиксированным пинам: любой канал маршрутизируется на любой GPIO-пад через GPIO Matrix по своему индексу выходного сигнала (`LEDC_HS_SIG_OUT0-7` / `LEDC_LS_SIG_OUT0-7`).

## Реализованный объём

- Смоделированы все LEDC-каналы и таймеры ESP32 (16 + 8), ESP32-S3 (8 + 4) и ESP32-C3 (6 + 4) с правильными смещениями регистров для каждой группы: у ESP32 есть HS-группа по `0x0000` и LS-группа по `0x00A0` (HS-таймеры по `0x140`, LS-таймеры по `0x160`); у ESP32-S3/C3 есть одна LS-группа по `0x0000` с таймерами по `0x00A0`. Шаг канала — `0x14`.
- Привязка канала к таймеру соответствует аппаратному полю `TIMER_SEL` (биты 1:0 в `CONF0`): HS-каналы ESP32 выбирают HS-таймеры 0-3, LS-каналы выбирают LS-таймеры 4-7; каналы ESP32-S3/C3 выбирают таймеры своей единственной группы.
- Учитываются `SIG_OUT_EN`, `IDLE_LV` и `PAUSE`/`RST` таймера: выключенный канал выдаёт свой `IDLE_LV`, остановленный или сброшенный таймер не работает.
- Частота вычисляется по формуле фиксированного делителя выше. ESP32 поддерживает APB (80 МГц) и REF_TICK (1 МГц); ESP32-S3/C3 дополнительно декодируют глобальный mux `apb_clk_sel` для APB (80 МГц), RC_FAST (8 МГц) и XTAL (40 МГц). `clock_divider` и `duty_res` извлекаются из реальных битовых полей.
- Duty покрывает 0-100 %: `DUTY >> 4` тиков высокого уровня относительно полной шкалы `2^duty_res`. Младшая тетрада синтезируется с аппаратным 16-периодным дробным dithering: значение `N` создаёт ровно `N` импульсов на один тик длиннее за 16 PWM-периодов. Регистр `DUTY` также доступен прошивке для чтения (`DUTY_R`).
- Блок регистров LEDC передаётся из QEMU в SimulIDE через мост (ESP32 по `0x00059000`, ESP32-S3/C3 по `0x00019000`), а каждый выходной канал доступен GPIO Matrix по своему индексу сигнала из datasheet (ESP32 71-86, ESP32-S3 73-80, ESP32-C3 45-50), поэтому маршрутизация на любой GPIO работает как на реальном железе.
- Параметры low-speed каналов и таймеров используют аппаратные strobes `PARA_UP`. Параметры high-speed ESP32 обновляются сразу.
- Смоделированы `HPOINT`, `VALUE` таймера, duty fade/scale, статусы timer/fade/overflow-count, а также `INT_RAW`/`INT_ST`/`INT_ENA`/`INT_CLR`. LEDC-прерывания маршрутизируются в семейно-специфичный источник прерываний.
- Смоделированы биты 8:0 выходного селектора GPIO Matrix, инверсия выхода для каждого пада, выбор/инверсия output-enable и one-to-many fan-out LEDC. ESP32-S3/C3 также моделируют свою GPIO-функцию IO_MUX по адресу `0x60009000`.
- Сброс перепривязывает каждый канал к таймеру 0, очищает подписки matrix и состояние IRQ, а также отменяет устаревшие события. Изменение работающего таймера перепланирует его на новый период.
- ESP8266 FRC1 моделируется как 23-битный countdown-таймер с перезагрузкой LOAD, делителями `/1`/`/16`/`/256`, one-shot/autoreload поведением и обычным IRQ9. Машина ESP8266 принимает ELF, Espressif `0xE9` и плоские IRAM-образы.

## Известные ограничения

- Fan-out LEDC и выходы SPI, I2C и UART matrix подключены через endpoint-модель GPIO Matrix `Esp32OutputSignal`/`Esp32InputSignal`. MCPWM и остальные нереализованные выходы matrix (RMT, I2S, PCNT, TWAI, EMAC, SDIO) пока являются только label-only `nullptr` записями без модуля SimulIDE.
- У ESP8266 нет аппаратной PWM-периферии. Его таймер FRC1 и обычный IRQ9 доступны для самодостаточных прошивок, но штатный Arduino `analogWrite()` всё ещё не поддерживается, потому что у машины нет boot ROM ESP8266, выполнения из flash-cache или инфраструктуры dispatch для `NmiTimSetFunc`/NMI14.

## Официальные источники

- [ESP32 Series Datasheet v5.3](https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf)
- [ESP32 Technical Reference Manual v5.8](https://www.espressif.com/sites/default/files/documentation/esp32_technical_reference_manual_en.pdf)
- [ESP32-S3 Series Datasheet v2.2](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf)
- [ESP32-S3 Technical Reference Manual v1.8](https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf)
- [ESP32-C3 Series Datasheet v2.4](https://www.espressif.com/sites/default/files/documentation/esp32-c3_datasheet_en.pdf)
- [ESP32-C3 Technical Reference Manual v1.4](https://www.espressif.com/sites/default/files/documentation/esp32-c3_technical_reference_manual_en.pdf)
