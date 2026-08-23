/***************************************************************************
 *   Copyright (C) 2025 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include <array>

#include "esp32pin.h"
#include "qemuspi.h"

class Esp32Spi : public QemuSpi {
public:
    Esp32Spi( QemuDevice* mcu, QString name, int n, uint32_t* clk, uint64_t memStart, uint64_t memEnd,
              bool modern = false, bool esp8266 = false );
    ~Esp32Spi();

    void reset() override;
    void connected( bool c ) override;
    void setMode( spiMode_t mode ) override;
    void ssChanged( bool enable ) override;

    void endTransaction() override;

    Esp32OutputSignal* getCkOutputSignal() { return &m_ckOutput; }
    Esp32OutputSignal* getMiOutputSignal() { return &m_miOutput; }
    Esp32OutputSignal* getMoOutputSignal() { return &m_moOutput; }
    Esp32OutputSignal* getSsOutputSignal() { return &m_ssOutput; }
    Esp32InputSignal* getCkInputSignal() { return &m_ckInput; }
    Esp32InputSignal* getMiInputSignal() { return &m_miInput; }
    Esp32InputSignal* getMoInputSignal() { return &m_moInput; }
    Esp32InputSignal* getSsInputSignal() { return &m_ssInput; }

private:
    void writeRegister() override;
    void readRegister() override;

    void configureClock();
    void configureMode();
    void startUserTransaction();
    void loadByte();
    void updateOutputEnables();

    void driveClock( bool state ) override;
    void driveData( bool state ) override;
    void driveSelect( bool state ) override;
    bool sampleClock() override;
    bool sampleData() override;
    bool sampleSelect() override;
    bool drivenClock() override;
    void watchClock( eElement* listener, bool enabled ) override;
    void watchSelect( eElement* listener, bool enabled ) override;

    std::array<uint32_t, 16> m_data;
    bool m_modern;
    bool m_esp8266;
    bool m_transactionActive;
    uint8_t m_dataIndex;
    uint8_t m_dataBytes;
    Esp32OutputSignal m_ckOutput;
    Esp32OutputSignal m_miOutput;
    Esp32OutputSignal m_moOutput;
    Esp32OutputSignal m_ssOutput;
    Esp32InputSignal m_ckInput;
    Esp32InputSignal m_miInput;
    Esp32InputSignal m_moInput;
    Esp32InputSignal m_ssInput;
};
