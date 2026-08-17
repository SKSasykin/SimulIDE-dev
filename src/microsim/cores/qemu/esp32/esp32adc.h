/***************************************************************************
 *   Copyright (C) 2026 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "esp32gpio.h"
#include "esp32pin.h"
#include "qemumodule.h"

enum Esp32AdcMode {
    Esp32AdcEsp32,
    Esp32AdcS3,
    Esp32AdcC3
};

class Esp32Adc : public QemuModule {
public:
    Esp32Adc( QemuDevice* mcu, QString name, int n, uint32_t* clk = nullptr, uint64_t memStart = 0,
              uint64_t memEnd = 0, Esp32Gpio* gpio = nullptr, Esp32AdcMode mode = Esp32AdcEsp32 );
    ~Esp32Adc();

protected:
    void writeRegister() override;
    void readRegister() override;

    uint16_t getRaw( int gpio, int atten, int bits = 12 );
    void convert( int adcNum );
    void convertC3();

    Esp32Gpio* m_gpio;
    Esp32AdcMode m_mode;

    uint32_t m_start1Reg;
    uint32_t m_start2Reg;
    uint32_t m_atten1Reg;
    uint32_t m_atten2Reg;
    uint32_t m_data1Reg;
    uint32_t m_data2Reg;

    int  m_adc1Chans;
    int  m_adc2Chans;
    const int* m_adc1Gpio;
    const int* m_adc2Gpio;

    uint16_t m_meas1Data;
    uint16_t m_meas2Data;
    uint8_t  m_meas1Done;
    uint8_t  m_meas2Done;

    uint32_t m_measStart1;
    uint32_t m_measStart2;
    uint32_t m_atten1;
    uint32_t m_atten2;
};
