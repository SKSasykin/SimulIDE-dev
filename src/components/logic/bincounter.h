/***************************************************************************
 *   Copyright (C) 2025 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "logiccomponent.h"

class LibraryItem;

class BinCounter : public LogicComponent {
public:
    BinCounter( QString type, QString id );
    ~BinCounter();

    static Component* construct( QString type, QString id );
    static LibraryItem* libraryItem();

    bool propNotFound( QString prop, QString val ) override;

    int bits() { return m_bits; }
    void setBits( int b );

    bool parallelIn() { return m_parallelIn; }
    void setParallelIn( bool p );

    bool useRCO() { return m_useRCO; }
    void setUseRCO( bool rco );

    bool bidirectional() { return m_bidirectional; }
    void setBidirectional( bool b );

    //bool srInv() { return m_resetInv; }
    //void setSrInv( bool inv );

    void stamp() override;
    void voltChanged() override;
    void runEvent() override { IoComponent::runOutputs(); }

    int maxVal() { return m_topValue; }
    void setMaxVal( int v ) { m_topValue = v; }

private:
    void updatePins();

    int m_bits;
    int m_counter;
    int m_topValue;

    bool m_useRCO;
    //bool m_resetInv;
    bool m_parallelIn;
    bool m_bidirectional;

    IoPin* m_dirPin;
    IoPin* m_ldPin;
    IoPin* m_rstPin;
    IoPin* m_rcoPin;
    IoPin* m_rboPin;
};
