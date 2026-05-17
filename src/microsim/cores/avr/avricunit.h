/***************************************************************************
 *   Copyright (C) 2021 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "mcuicunit.h"

class AvrIcUnit : public McuIcUnit {
public:
    AvrIcUnit( eMcu* mcu, QString name );
    virtual ~AvrIcUnit();

    virtual void configure( uint8_t val ) override;

protected:
    regBits_t m_ICbits;
};
