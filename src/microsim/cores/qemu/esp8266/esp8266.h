/***************************************************************************
 *   Copyright (C) 2026 by Santiago González                               *
 *                                                                         *
 *   ( see copyright.txt file at root folder )                             *
 ***************************************************************************/

#pragma once

#include "esp32pin.h"
#include "qemudevice.h"

class LibraryItem;
class Esp8266Adc;
class Esp8266Gpio;
class Esp8266IoMux;
class Esp8266Rtc;
class IoPin;

class Esp8266 : public QemuDevice {
    public:
        Esp8266( QString type, QString id, QString device );
        ~Esp8266();

        void stamp() override;

    protected:
        Pin* addPin( QString id, QString type, QString label, int n, int x, int y, int angle, int length = 8,
                     int space = 0 ) override;

        bool createArgs() override;

        void updtFrequency() override;

        uint32_t m_cpuFreq;
        uint32_t m_apbFreq;

        Esp8266Gpio* m_gpio;
        Esp8266IoMux* m_iomux;
        Esp8266Rtc* m_rtc;
        Esp8266Adc* m_adc;
        IoPin* m_adcPin;
};
