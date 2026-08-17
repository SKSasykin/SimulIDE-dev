/***************************************************************************
 *   Copyright (C) 2025 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include <array>

#include "qemuspi.h"

class Esp32Spi : public QemuSpi {
public:
    Esp32Spi( QemuDevice* mcu, QString name, int n, uint32_t* clk, uint64_t memStart, uint64_t memEnd,
              bool modern = false, bool esp8266 = false );
    ~Esp32Spi();

    void reset() override;
    void connected( bool c ) override;

    void endTransaction() override;

private:
    void writeRegister() override;
    void readRegister() override;

    void configureClock();
    void configureMode();
    void startUserTransaction();
    void loadByte();

    std::array<uint32_t, 16> m_data;
    bool m_modern;
    bool m_esp8266;
    bool m_transactionActive;
    uint8_t m_dataIndex;
    uint8_t m_dataBytes;
};
