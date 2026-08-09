
VERSION = "2.0.0"
RELEASE = ""

TEMPLATE = app
TARGET = simulide

QT += svg
QT += xml
QT += widgets
QT += concurrent
QT += serialport
QT += multimedia widgets

SOURCES      = $$files( $$PWD/src/*.cpp, true )
HEADERS      = $$files( $$PWD/src/*.h, true )
TRANSLATIONS = $$files( $$PWD/resources/translations/*.ts )
FORMS       += $$files( $$PWD/src/*.ui, true )
RESOURCES    = $$PWD/src/application.qrc

INCLUDEPATH += $$PWD/src \
    $$PWD/src/components \
    $$PWD/src/components/active \
    $$PWD/src/components/connectors \
    $$PWD/src/components/graphical \
    $$PWD/src/components/logic \
    $$PWD/src/components/meters \
    $$PWD/src/components/micro \
    $$PWD/src/components/other \
    $$PWD/src/components/other/truthtable \
    $$PWD/src/components/outputs \
    $$PWD/src/components/outputs/displays \
    $$PWD/src/components/outputs/leds \
    $$PWD/src/components/outputs/motors \
    $$PWD/src/components/passive \
    $$PWD/src/components/passive/reactive \
    $$PWD/src/components/passive/resistors \
    $$PWD/src/components/passive/resist_sensors \
    $$PWD/src/components/sources \
    $$PWD/src/components/subcircuits \
    $$PWD/src/components/switches \
    $$PWD/src/gui \
    $$PWD/src/gui/appdialogs \
    $$PWD/src/gui/circuitwidget \
    $$PWD/src/gui/componentlist \
    $$PWD/src/gui/dataplotwidget \
    $$PWD/src/gui/editorwidget \
    $$PWD/src/gui/editorwidget/debuggers \
    $$PWD/src/gui/editorwidget/dialogs \
    $$PWD/src/gui/filebrowser \
    $$PWD/src/gui/memory \
    $$PWD/src/gui/properties \
    $$PWD/src/gui/serial \
    $$PWD/src/gui/testing \
    $$PWD/src/simulator \
    $$PWD/src/simulator/elements \
    $$PWD/src/simulator/elements/active \
    $$PWD/src/simulator/elements/outputs \
    $$PWD/src/simulator/elements/passive \
    $$PWD/src/microsim \
    $$PWD/src/microsim/cores \
    $$PWD/src/microsim/cores/avr \
    $$PWD/src/microsim/cores/i51 \
    $$PWD/src/microsim/cores/pic \
    $$PWD/src/microsim/cores/mcs65 \
    $$PWD/src/microsim/cores/z80 \
    $$PWD/src/microsim/cores/scripted \
    $$PWD/src/microsim/cores/qemu \
    $$PWD/src/microsim/cores/qemu/esp32 \
    $$PWD/src/microsim/cores/qemu/esp8266 \
    $$PWD/src/microsim/cores/qemu/stm32 \
    $$PWD/src/microsim/modules \
    $$PWD/src/microsim/modules/memory \
    $$PWD/src/microsim/modules/usart \
    $$PWD/src/microsim/modules/onewire\
    $$PWD/src/microsim/modules/twi \
    $$PWD/src/microsim/modules/tcp\
    $$PWD/src/microsim/modules/spi\
    $$PWD/src/microsim/modules/script\
    $$PWD/src/angel/include \
    $$PWD/src/angel/JIT \
    $$PWD/src/angel/src

QMAKE_CXXFLAGS += -Wno-unused-parameter
#QMAKE_CXXFLAGS += -Wno-deprecated-declarations
QMAKE_CXXFLAGS += -Wno-implicit-fallthrough
QMAKE_CXXFLAGS += -fno-strict-aliasing      #AngelScript
QMAKE_CXXFLAGS += -Wno-cast-function-type   #AngelScript
QMAKE_CXXFLAGS += -Wno-deprecated-copy      #AngelScript
QMAKE_CXXFLAGS += -Wno-invalid-offsetof     #AngelScript
QMAKE_CXXFLAGS += -Ofast
QMAKE_CXXFLAGS_DEBUG += -D_GLIBCXX_ASSERTIONS
QMAKE_CXXFLAGS_DEBUG -= -O
QMAKE_CXXFLAGS_DEBUG -= -O1
QMAKE_CXXFLAGS_DEBUG -= -O2
QMAKE_CXXFLAGS_DEBUG -= -O3
QMAKE_CXXFLAGS_DEBUG += -O0

LIBS += -lz

win32 {
    OS = Windows
    QMAKE_LIBS += -lwsock32
    RC_ICONS += $$PWD/resources/icons/simulide.ico
}
linux {
    OS = Linux
}
macx {
    OS = MacOs
    ICON = $$PWD/resources/icons/simulide.icns
    appdata.files = $$PWD/resources/data
    appdata.path = Contents/MacOS
    QMAKE_BUNDLE_DATA += appdata
}

contains( QMAKE_HOST.arch, arm64|aarch64 ) | contains( QMAKE_CC, .*aarch64.* ){
    macx {
        SOURCES += $$PWD/src/angel/src/as_callfunc_arm64_xcode.S
    } else {
        SOURCES += $$PWD/src/angel/src/as_callfunc_arm64_gcc.S
    }
}

contains( QMAKE_HOST.os, Windows ) {
    REV_NO = $$system("powershell -NoProfile -Command Get-Date -Format yyMMdd")       # year-month-day
    BUILD_TIME = $$system("powershell -NoProfile -Command Get-Date -Format HHmm")     # hour-minute
    BUILD_DATE = $$system("powershell -NoProfile -Command Get-Date -Format dd-MM-yy") # day-month-year
}
else {
    REV_NO = $$system($(which date) +%y%m%d)
    BUILD_TIME = $$system($(which date) +%H%M)
    BUILD_DATE = $$system($(which date) +%d-%m-%y)
}

BUILD_STAMP = $${REV_NO}.$${BUILD_TIME}

CONFIG += qt 
CONFIG += warn_on
CONFIG += no_qml_debug
CONFIG *= c++11

DEFINES += REVNO=\\\"$$REV_NO\\\"
DEFINES += APP_VERSION=\\\"$$VERSION-$$RELEASE\\\"
DEFINES += BUILDDATE=\\\"$$BUILD_DATE\\\"
DEFINES += BUILD_STAMP=\\\"$$BUILD_STAMP\\\"

TARGET = simulide-$$BUILD_STAMP

OBJECTS_DIR *= $$OUT_PWD/build/objects
MOC_DIR     *= $$OUT_PWD/build/moc
INCLUDEPATH += $$MOC_DIR

DESTDIR = $$BUILD_DIR/executables

runLrelease.commands = \
    lrelease $$PWD/resources/translations/*.ts; \
    lrelease $$PWD/resources/translations/qt/*.ts; \
    $(MOVE) $$PWD/resources/translations/*.qm $$PWD/resources/qm; \
    $(MOVE) $$PWD/resources/translations/qt/*.qm $$PWD/resources/qm;

QMAKE_EXTRA_TARGETS += runLrelease
PRE_TARGETDEPS      += runLrelease

# Build the qemu emulator binaries (third_party/qemu-simulide submodule + patch)
# if they are missing from resources/data/bin. Runtime-only dependency: a
# failure here must not break the SimulIDE build, hence `|| true`.
macx|linux {
    runQemuBuild.commands = \
        bash $$PWD/scripts/build_qemu.sh || true
    QMAKE_EXTRA_TARGETS += runQemuBuild
    PRE_TARGETDEPS      += runQemuBuild
}

message( "-----------------------------------")
message( "    "                               )
message( "    "$$TARGET for $$OS         )
message( "    "                               )
message( "    Host:      "$$QMAKE_HOST.os     )
message( "    Date:      "$$BUILD_DATE        )
message( "    Qt version: "$$QT_VERSION       )
message( "    "                               )
message( "    Destination Folder:"            )
message( $$DESTDIR                           )
message( "-----------------------------------")
