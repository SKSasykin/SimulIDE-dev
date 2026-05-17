/***************************************************************************
 *   Copyright (C) 2016 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "logiccomponent.h"

class LibraryItem;

class Counter : public LogicComponent {
public:
    Counter( QString type, QString id );
    ~Counter();

    static Component* construct( QString type, QString id );
    static LibraryItem* libraryItem();

    //bool srInv() { return m_resetInv; }
    //void setSrInv( bool inv );

    bool propNotFound( QString prop, QString val ) override;

    bool pinSet() { return m_pinSet; }
    void useSetPin( bool set );

    void stamp() override;
    void voltChanged() override;
    void runEvent() override { IoComponent::runOutputs(); }

    int maxVal() { return m_TopValue; }
    void setMaxVal( int v ) { m_TopValue = v; }

protected:
    int m_Counter;
    int m_TopValue;

    //bool m_resetInv;
    bool m_pinSet;

    IoPin* m_setPin;
    IoPin* m_rstPin;
};
