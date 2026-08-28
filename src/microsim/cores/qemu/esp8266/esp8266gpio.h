/***************************************************************************
 *   Copyright (C) 2026 by Santiago González                               *
 *                                                                         *
 *   ( see copyright.txt file at root folder )                             *
 ***************************************************************************/

#pragma once

#include "e-element.h"
#include "esp32pin.h"
#include "qemumodule.h"

class Esp8266;
class QemuDevice;

class Esp8266Gpio : public QemuModule, public eElement
{
    friend Esp8266;

    public:
        Esp8266Gpio( QemuDevice* mcu, QString name, int n, uint32_t* clk,
                     uint64_t memStart, uint64_t memEnd, int nPins );
        ~Esp8266Gpio();

        void reset() override;

    static int gpioFromId( const QString& id );

        uint32_t readPort();

        Esp32Pin* getPad( int gpio ) {
            return gpio >= 0 && gpio < (int)m_espPad.size() ? m_espPad[gpio] : nullptr;
        }

    protected:
        void writeRegister() override;
        void readRegister() override;

        Esp32Pin* createPin( int i, QString id, QemuDevice* mcu );

        void setGpioState( uint32_t state );
        void setGpioDir( uint32_t dir );

        std::vector<Esp32Pin*> m_pins;
        std::vector<Esp32Pin*> m_espPad;
        Esp32Pin* m_dummyPin;

        int m_nPins;

        uint32_t m_gpioState;
        uint32_t m_gpioEnable;
        uint32_t m_strapMode;
};
