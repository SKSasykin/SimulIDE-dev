/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "reactive.h"

class LibraryItem;

class Inductor : public Reactive {
public:
    Inductor( QString type, QString id );
    ~Inductor();

    static Component* construct( QString type, QString id );
    static LibraryItem* libraryItem();

    double indCurrent() { return m_curSource; }

    virtual void setCurrentValue( double c ) override;

    Pin* getPin( int n ) { return m_pin[n]; }

    virtual void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;

protected:
    double updtRes() override { return m_inductance / m_tStep; }
    double updtCurr() override { return m_curSource - m_volt * m_admit; }

    double m_inductance;
};
