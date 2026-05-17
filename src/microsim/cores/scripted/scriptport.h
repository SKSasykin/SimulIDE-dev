/***************************************************************************
 *   Copyright (C) 2022 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "mcuport.h"
#include "scriptbase.h"

class ScriptPort : public ScriptBase, public McuPort {
public:
    ScriptPort( eMcu* mcu, QString name );
    ~ScriptPort();

    virtual void configureA( uint8_t newVal ) override;
    virtual void configureB( uint8_t newVal ) override;
    virtual void configureC( uint8_t newVal ) override;

    virtual void reset() override;

    virtual void setScript( QString script ) override;

    void setExtIntTrig( int pinNumber, int trig );
};
