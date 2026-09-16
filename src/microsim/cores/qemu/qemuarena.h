/***************************************************************************
 *   Copyright (C) 2025 by Santiago Gonzalez                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include <cstdint>

// Shared packet rings used by the SimulIDE host and the QEMU process.
// tail is written by the producer and head by the consumer.
#define QEMU_WIFI_RING_FRAMES 64
#define QEMU_WIFI_FRAME_MAX   1536

typedef struct qemuWifiFrame {
    uint32_t len;
    uint8_t  data[QEMU_WIFI_FRAME_MAX];
} qemuWifiFrame_t;

typedef struct qemuWifiRing {
    volatile uint32_t     head;
    volatile uint32_t     tail;
    volatile uint64_t     seq;
    qemuWifiFrame_t       frames[QEMU_WIFI_RING_FRAMES];
} qemuWifiRing_t;

#define QEMU_IRQ_RING_EVENTS 256

typedef struct qemuIrqEvent {
    uint32_t number;
    uint32_t level;
} qemuIrqEvent_t;

typedef struct qemuIrqRing {
    volatile uint32_t head;
    volatile uint32_t tail;
    qemuIrqEvent_t events[QEMU_IRQ_RING_EVENTS];
} qemuIrqRing_t;

typedef struct qemuArena {
    uint64_t simuTime;
    uint64_t qemuTime;
    uint64_t regData;
    uint64_t regAddr;
    uint64_t irqNumber;
    uint64_t irqLevel;
    uint64_t simuAction;
    uint64_t qemuAction;
    uint64_t running;
    int64_t loop_timeout_ns;
    double ps_per_inst;

    qemuIrqRing_t irq;
    qemuWifiRing_t wifi_rx;
    qemuWifiRing_t wifi_tx;
    qemuWifiRing_t bt_rx;
    qemuWifiRing_t bt_tx;
} qemuArena_t;

enum simuAction {
    SIM_NONE = 0,
    SIM_READ,
    SIM_WRITE,
    SIM_FREQ,
    SIM_INTERRUPT,
    SIM_I2C = 10,
    SIM_SPI,
    SIM_USART,
    SIM_TIMER,
    SIM_GPIO_IN,
    SIM_WIFI = 12,
    SIM_BT = 13,
    SIM_EVENT = 1 << 7
};
