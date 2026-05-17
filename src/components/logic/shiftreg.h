/***************************************************************************
 *   Copyright (C) 2016 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "logiccomponent.h"

class LibraryItem;

class ShiftReg : public LogicComponent {
public:
    ShiftReg( QString type, QString id );
    ~ShiftReg();

    static Component* construct( QString type, QString id );
    static LibraryItem* libraryItem();

    bool propNotFound( QString prop, QString val ) override;

    void stamp() override;
    void voltChanged() override;
    void runEvent() override { IoComponent::runOutputs(); }

    int bits() { return m_bits; }
    void setBits( int b );

    bool parallelIn() { return m_parallelIn; }
    void setParallelIn( bool p );

    bool bidirectional() { return m_bidir; }
    void setBidirectional( bool b );

    //bool resetInv() { return m_resetInv; }
    //void setResetInv( bool inv );

private:
    void updatePins();

    int m_bits;
    uint m_bitMask;

    //bool m_resetInv;
    bool m_parallelIn;
    bool m_bidir;
    bool m_ldInps;

    IoPin* m_dinPin;
    IoPin* m_dilPin;
    IoPin* m_dirPin;
    IoPin* m_rstPin;
    IoPin* m_serPin;
};
