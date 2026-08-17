/***************************************************************************
 *   Copyright (C) 2026 by Santiago González                               *
 *                                                                         *
 *   ( see copyright.txt file at root folder )                             *
 ***************************************************************************/

#pragma once

#include "qemumodule.h"

class IoPin;

class Esp8266Adc : public QemuModule
{
    public:
        Esp8266Adc( QemuDevice* mcu, QString name, int n, uint32_t* clk,
                    uint64_t memStart, uint64_t memEnd, IoPin* pin );
        ~Esp8266Adc();

    protected:
        void writeRegister() override;
        void readRegister() override;

        void convert();
        uint16_t decodeSample( uint16_t encoded ) const;

        IoPin* m_pin;
};
