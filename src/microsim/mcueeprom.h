/***************************************************************************
 *   Copyright (C) 2021 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "e-element.h"
#include "mcumodule.h"

class McuEeprom : public McuModule, public eElement {
    friend class McuCreator;

public:
    McuEeprom( eMcu* mcu, QString name );
    virtual ~McuEeprom();

    virtual void initialize() override;

    virtual void readEeprom();
    virtual void writeEeprom();

    virtual void addrWriteL( uint8_t val );
    virtual void addrWriteH( uint8_t val );

protected:
    uint8_t* m_addressL; // Actual ram for counter Low address byte
    uint8_t* m_addressH; // Actual ram for counter High address byte

    uint32_t m_address;
    uint8_t* m_dataReg;
};
