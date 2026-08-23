/***************************************************************************
 *   Copyright (C) 2025 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include <deque>

#include "esp32pin.h"
#include "qemutwi.h"

struct esp32TwiArena_t {
    uint8_t state;
};

class Esp32Twi : public QemuTwi {
    friend class I2cRunner;

public:
    Esp32Twi( QemuDevice* mcu, QString n, int number, uint32_t* clk, uint64_t memStart, uint64_t memEnd,
              bool modern = false, int interrupt = -1 );
    ~Esp32Twi();

    void reset() override;

    void connected( bool c ) override;
    void setMode( twiMode_t mode ) override;

    Esp32OutputSignal* getSclOutputSignal() { return &m_sclOutput; }
    Esp32OutputSignal* getSdaOutputSignal() { return &m_sdaOutput; }
    Esp32InputSignal* getSclInputSignal() { return &m_sclInput; }
    Esp32InputSignal* getSdaInputSignal() { return &m_sdaInput; }

protected:
    void writeRegister() override;
    void readRegister() override;

    void writeCTR();
    void startTransaction();
    void runCommand();
    bool writeNextByte();
    void readNextByte();
    void commandDone();
    void finishTransaction( uint32_t interruptMask = 1 << 7, bool releaseBus = true );
    void updateInterrupt();
    void setPeriod();

    void setTwiState( twiState_t state ) override;

    void driveScl( bool state, uint64_t delay ) override;
    void driveSda( bool state, uint64_t delay ) override;
    bool sampleScl() override;
    bool sampleSda() override;
    void watchLines( eElement* listener, bool enabled ) override;

    std::deque<uint8_t> m_txFifo;
    std::deque<uint8_t> m_rxFifo;

    bool m_modern;
    int m_interrupt;
    bool m_busy;
    bool m_busBusy;
    bool m_expectAddress;
    bool m_ackCheck;
    uint8_t m_commandIndex;
    uint8_t m_commandCount;
    uint8_t m_remaining;
    uint32_t m_interruptRaw;
    uint32_t m_interruptEnable;
    Esp32OutputSignal m_sclOutput;
    Esp32OutputSignal m_sdaOutput;
    Esp32InputSignal m_sclInput;
    Esp32InputSignal m_sdaInput;
};
