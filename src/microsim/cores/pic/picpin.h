/***************************************************************************
 *   Copyright (C) 2020 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "mcupin.h"

class Component;
class McuPort;
class eMcu;

class PicPin : public McuPin {
    friend class McuPort;

public:
    PicPin( McuPort* port, int i, QString id, Component* mcu );
    ~PicPin();

    virtual void ConfExtInt( uint8_t bits ) override;

    virtual void setAnalog( bool an ) override;
};
