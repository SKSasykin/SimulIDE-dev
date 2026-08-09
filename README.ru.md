# SimulIDE

Симулятор электронных схем

**SimulIDE — простой симулятор электронных схем в реальном времени**, предназначенный для любителей и студентов, изучающих аналоговые и цифровые схемы и микроконтроллеры.
Поддерживает PIC, AVR, Arduino и другие микроконтроллеры и микропроцессоры.

**Простота, скорость и удобство** — ключевые особенности этого симулятора.
Вы можете создавать, симулировать и взаимодействовать со своими схемами за считанные минуты: просто перетащите компоненты из списка на схему, соедините их и нажмите кнопку «питание», чтобы увидеть, как всё работает.

Скорость симуляции — одна из главных характеристик этого симулятора.
Она глубоко оптимизирована для достижения отличной скорости и низкого использования процессора.

SimulIDE также включает редактор кода и отладчик для Arduino, GcBasic, PIC asm, AVR asm и других. Можно писать, компилировать и выполнять базовую отладку с точками останова, просмотром регистров и глобальных переменных.


## Сборка SimulIDE:

Зависимости для сборки:

 - Пакеты Qt5 для разработки
 - Qt5Core
 - Qt5Gui
 - Qt5Xml
 - Qt5Widgets
 - Qt5Concurrent
 - Qt5svg dev
 - Qt5 Multimedia dev
 - Qt5 Serialport dev
 - Qt5 qmake
 - Git (для загрузки QEMU-подмодуля)
 - C/C++ компилятор, python3 с pip, ninja и pkg-config
 - libgcrypt >= 1.8 (для эмуляции ESP32/STM32; macOS: `brew install libgcrypt`,
   Debian/Ubuntu: `libgcrypt20-dev`, Fedora: `libgcrypt-devel`)
 - Стандартные зависимости сборки QEMU: пакеты разработки glib-2.0 и pixman

На macOS (Homebrew) всё вышеперечисленное устанавливается так:

```
$ xcode-select --install
$ brew install qt@5 libgcrypt glib pixman ninja pkgconf python3
```

Примечание: `qt@5` — keg-only, поэтому сначала добавьте его bin в `PATH`:

```
$ export PATH="/opt/homebrew/opt/qt@5/bin:$PATH"
```

После установки инициализируйте QEMU-подмодуль из корня репозитория:

```
$ git submodule update --init --recursive
```

Затем перейдите в папку build:

```
$ qmake
$ make
```

Шаг `make` автоматически собирает бинарники QEMU-эмулятора
(`qemu-system-xtensa` для ESP32 / ESP32-S3, `qemu-system-riscv32` для
ESP32-C3, `qemu-system-arm` для STM32) из подмодуля
`third_party/qemu-simulide` в `resources/data/bin/` перед линковкой
бинарника SimulIDE. На macOS эмуляторы подписываются entitlement'ом
`com.apple.security.cs.allow-jit` (`scripts/qemu-jit.entitlements`); без
него TCG-движок QEMU завис бы при запуске на Apple Silicon. QEMU —
зависимость только времени выполнения, поэтому
сбой сборки эмулятора не останавливает основную сборку (запустите
`./scripts/build_qemu.sh` вручную, чтобы увидеть ошибку). Если бинарники уже
существуют и актуальны, этот шаг пропускается.

В папке build/executables вы найдёте исполняемый файл с именем
`simulide-YYMMDD.HHMM` (на macOS это
`build/executables/simulide-YYMMDD.HHMM.app`, с папкой данных внутри по пути
`Contents/MacOS/data`).



## Запуск SimulIDE:

Зависимости времени выполнения:

 - Qt5Core
 - Qt5Gui
 - Qt5Xml
 - Qt5svg
 - Qt5Widgets
 - Qt5Concurrent
 - Qt5 Multimedia
 - Qt5 Multimedia Plugins
 - Qt5 Serialport


Установка не требуется: поместите папку SimulIDE в любое место и запустите исполняемый файл.


## Эмуляция ESP32 / STM32 (QEMU):

Микроконтроллеры ESP32, ESP32-S3 (Xtensa) и ESP32-C3 (RISC-V), а также
STM32 (ARM) эмулируются форком QEMU.
Форк находится в git-подмодуле `third_party/qemu-simulide`
(https://github.com/SKSasykin/SimulIDE-qemu), закреплённом на коммите
`8a3b5e7`. Наши доработки (закоммичены непосредственно в форк) добавляют
мост к разделяемой памяти SimulIDE, сопоставление AHB-шины с UART-FIFO,
контроллер SDIO-slave (SLC), варианты моста для ESP32-S3
(`esp32s3-simulide-bridge`) и ESP32-C3 (`esp32c3-simulide-bridge`), а также
различные исправления сборки; внешний файл патча не нужен.

ROM-дампы ESP32 (`data/bin/esp/rom/bin/*.bin`) копируются автоматически из
каталога `pc-bios/` форка скриптом `scripts/build_qemu.sh` при каждой сборке,
так что добавлять или коммитить их вручную не нужно.

Для ESP32 нужен образ flash-памяти размером 4 МБ. Рекомендуемый формат —
`merged.bin`, создаваемый Arduino IDE / arduino-cli (загрузчик по адресу
0x1000, таблица разделов по 0x8000, приложение по 0x10000). SimulIDE
автоматически дополняет меньшие валидные бинарники до 4 МБ и выводит
понятную ошибку, если файл отсутствует, пуст или больше 4 МБ. Если
сконфигурированный файл прошивки не найден, используется встроенная
примерная прошивка, так что пустая плата всё равно загрузится и начнёт
мигать: `data/bin/esp32/blink.ino.merged.bin` для ESP32,
`data/bin/esp32s3/blink.ino.merged.bin` для ESP32-S3 и
`data/bin/esp32c3/blink.ino.merged.bin` для ESP32-C3.
