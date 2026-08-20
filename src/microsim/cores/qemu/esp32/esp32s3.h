/***************************************************************************
 *   Copyright (C) 2026 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "esp32pin.h"
#include "qemudevice.h"

class LibraryItem;
class Esp32Adc;
class Esp32Gpio;
class Esp32IoMux;
class Esp32Led;

class Esp32s3 : public QemuDevice {
public:
    Esp32s3( QString type, QString id, QString device );
    ~Esp32s3();

    void stamp() override;

protected:
    Pin* addPin( QString id, QString type, QString label, int n, int x, int y, int angle, int length = 8,
                 int space = 0 ) override;

    bool createArgs() override;

    void updtFrequency() override;

    void createMatrix();

    uint32_t m_cpuFreq;
    uint32_t m_apbFreq;

    Esp32Gpio* m_gpio;
    Esp32IoMux* m_iomux;
    Esp32Adc* m_adc;
    Esp32Led* m_leds;
};
