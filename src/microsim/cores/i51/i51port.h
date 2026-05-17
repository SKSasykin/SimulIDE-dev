/***************************************************************************
 *   Copyright (C) 2021 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "mcuport.h"

class I51Port : public McuPort {
public:
    I51Port( eMcu* mcu, QString name );
    ~I51Port();

    virtual void reset() override;

    virtual void readPort( uint8_t ) override;

protected:
    virtual McuPin* createPin( int i, QString id, Component* mcu ) override;
};
