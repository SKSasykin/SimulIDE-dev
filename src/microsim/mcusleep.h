/***************************************************************************
 *   Copyright (C) 2022 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "e-element.h"
#include "mcumodule.h"

class McuSleep : public McuModule, public eElement {
    friend class McuCreator;

public:
    McuSleep( eMcu* mcu, QString name );
    ~McuSleep();

    inline uint8_t mode() { return m_sleepMode; }

    bool enabled() { return m_enabled; }
    //virtual void sleep(){;}
    virtual void callBack() override;

protected:
    bool m_enabled;

    //uint8_t m_sleepMode; /// in McuModule
};
