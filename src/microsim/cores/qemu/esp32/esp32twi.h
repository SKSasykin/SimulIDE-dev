/***************************************************************************
 *   Copyright (C) 2025 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include <deque>

#include "qemutwi.h"

struct esp32TwiArena_t {
    uint8_t state;
};

class Esp32Twi : public QemuTwi {
    friend class I2cRunner;

public:
    Esp32Twi( QemuDevice* mcu, QString n, int number, uint32_t* clk, uint64_t memStart, uint64_t memEnd,
              bool modern = false );
    ~Esp32Twi();

    void reset() override;

    void connected( bool c ) override;

protected:
    void writeRegister() override;
    void readRegister() override;

    void writeCTR();
    void startTransaction();
    void runCommand();
    bool writeNextByte();
    void readNextByte();
    void commandDone();
    void finishTransaction();
    void setPeriod();

    void setTwiState( twiState_t state ) override;

    std::deque<uint8_t> m_txFifo;
    std::deque<uint8_t> m_rxFifo;

    bool m_modern;
    bool m_busy;
    bool m_expectAddress;
    bool m_ackCheck;
    uint8_t m_commandIndex;
    uint8_t m_commandCount;
    uint8_t m_remaining;
    uint32_t m_interruptRaw;
    uint32_t m_interruptEnable;
};
