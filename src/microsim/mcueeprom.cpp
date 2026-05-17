/***************************************************************************
 *   Copyright (C) 2021 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include "mcueeprom.h"
#include "e_mcu.h"
#include "simulator.h"

McuEeprom::McuEeprom( eMcu* mcu, QString name ) : McuModule( mcu, name ), eElement( mcu->getId() + "-" + name ) {
    m_addressL = nullptr;
    m_addressH = nullptr;
    m_dataReg = nullptr;
}

McuEeprom::~McuEeprom() { }

void McuEeprom::initialize() {
    m_address = 0;
}

void McuEeprom::readEeprom() {
    if ( m_dataReg )
        *m_dataReg = m_mcu->getRomValue( m_address );
}

void McuEeprom::writeEeprom() {
    m_mcu->setRomValue( m_address, *m_dataReg );
}

void McuEeprom::addrWriteL( uint8_t val ) {
    m_address = val;
    if ( m_addressH )
        m_address += *m_addressH << 8;
}

void McuEeprom::addrWriteH( uint8_t val ) {
    if ( m_addressL )
        m_address = ( val << 8 ) + *m_addressL;
}
