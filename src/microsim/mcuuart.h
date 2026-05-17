/***************************************************************************
 *   Copyright (C) 2020 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "mcumodule.h"
#include "usartmodule.h"

class McuUsart : public McuModule, public UsartModule {
    friend class McuCreator;

public:
    McuUsart( eMcu* mcu, QString name, int number );
    virtual ~McuUsart();

    virtual void sendByte( uint8_t data ) override { UsartModule::sendByte( data ); }
    virtual void bufferEmpty() override;
    virtual void frameSent( uint8_t data ) override;
    virtual void readByte( uint8_t data ) override;

    virtual uint8_t getBit9Tx() override;
    virtual void setBit9Rx( uint8_t bit ) override;

protected:
    int m_number;

    bool m_speedx2;

    regBits_t m_bit9Tx;
    regBits_t m_bit9Rx;
};
