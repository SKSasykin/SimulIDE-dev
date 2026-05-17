/***************************************************************************
 *   Copyright (C) 2020 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "mcuocunit.h"

class AvrOcUnit : public McuOcUnit {
    friend class McuCreator;

public:
    AvrOcUnit( eMcu* mcu, QString name );
    ~AvrOcUnit();

    virtual void initialize() override;
    virtual void configure( uint8_t val ) override;
    virtual void ocrWriteL( uint8_t val ) override;
    virtual void ocrWriteH( uint8_t val ) override;

    void setOcrMask( uint16_t mask );

protected:
    virtual void setPinSate( bool state, uint64_t time ) override;

    uint16_t m_OCRXmask;
};
