QT += core widgets
CONFIG += console c++11
CONFIG -= app_bundle
TEMPLATE = app
TARGET = qemubt-runtime

INCLUDEPATH += ../../src \
    ../../src/components \
    ../../src/components/subcircuits \
    ../../src/gui \
    ../../src/gui/circuitwidget \
    ../../src/gui/properties \
    ../../src/microsim/cores/qemu \
    ../../src/simulator \
    ../../src/simulator/elements

SOURCES += qemubt-runtime.cpp \
    ../../src/microsim/cores/qemu/qemubt.cpp \
    ../../src/microsim/cores/qemu/qemumodule.cpp \
    ../../src/microsim/cores/qemu/blechatformat.cpp \
    ../../src/microsim/cores/qemu/blechatclient.cpp

HEADERS += ../../src/microsim/cores/qemu/qemubt.h \
    ../../src/microsim/cores/qemu/qemumodule.h \
    ../../src/microsim/cores/qemu/qemuarena.h \
    ../../src/microsim/cores/qemu/blechatformat.h \
    ../../src/microsim/cores/qemu/blechatclient.h
