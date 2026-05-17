/***************************************************************************
 *   Copyright (C) 2020 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "mcupin.h"

class Component;
class McuPort;
class eMcu;

class AvrPin : public McuPin {
    friend class McuPort;

public:
    AvrPin( McuPort* port, int i, QString id, Component* mcu );
    ~AvrPin();

    virtual void setPortState( bool state ) override;
    virtual void ConfExtInt( uint8_t bits ) override;

protected:
};
