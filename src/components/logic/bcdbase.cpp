/***************************************************************************
 *   Copyright (C) 2018 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include "bcdbase.h"
#include "iopin.h"

BcdBase::BcdBase( QString type, QString id ) : LogicComponent( type, id ) {
    setCustomChars( "" ); // Load default chars

    m_digit = m_segments[0];
}
BcdBase::~BcdBase() { }

void BcdBase::initialize() {
    m_digit = m_segments[0];
    update();
}

void BcdBase::stamp() {
    LogicComponent::stamp();
    for ( uint i = 0; i < m_inpPin.size(); ++i )
        m_inpPin[i]->changeCallBack( this );

    m_changed = true;
}

void BcdBase::voltChanged() {
    m_changed = true;

    bool a = m_inpPin[0]->getInpState();
    bool b = m_inpPin[1]->getInpState();
    bool c = m_inpPin[2]->getInpState();
    bool d = m_inpPin[3]->getInpState();

    int index = a * 1 + b * 2 + c * 4 + d * 8;
    m_digit = m_segments[index];
}

void BcdBase::setCustomChars( QString chars ) {
    m_customChars = chars;
    if ( chars.isEmpty() ) {
        m_segments[0] = 0b00111111;
        m_segments[1] = 0b00000110;
        m_segments[2] = 0b01011011;
        m_segments[3] = 0b01001111;
        m_segments[4] = 0b01100110;
        m_segments[5] = 0b01101101;
        m_segments[6] = 0b01111101;
        m_segments[7] = 0b00000111;
        m_segments[8] = 0b01111111;
        m_segments[9] = 0b01101111;
        m_segments[10] = 0b01110111;
        m_segments[11] = 0b01111100;
        m_segments[12] = 0b00111001;
        m_segments[13] = 0b01011110;
        m_segments[14] = 0b01111001;
        m_segments[15] = 0b01110001;
        m_segments[16] = 0b00000000;
    } else {
        QStringList charList = chars.split( "," );
        int size = charList.size();
        for ( int i = 0; i < 16; ++i ) {
            if ( i >= size )
                m_segments[i] = 0;
            else
                m_segments[i] = charList[i].toInt();
        }
    }
    m_changed = true;
}
