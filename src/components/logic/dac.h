/***************************************************************************
 *   Copyright (C) 2017 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "logiccomponent.h"

class LibraryItem;

class DAC : public LogicComponent {
public:
    DAC( QString type, QString id );
    ~DAC();

    static Component* construct( QString type, QString id );
    static LibraryItem* libraryItem();

    void stamp() override;
    void voltChanged() override;
    void runEvent() override;

    double maxVolt() { return m_maxVolt; }
    void setMaxVolt( double v ) { m_maxVolt = v; }

    void setNumInputs( int pins );

protected:
    double m_maxVolt;
    double m_maxValue;

    int m_val;
};
