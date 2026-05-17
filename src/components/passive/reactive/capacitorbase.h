/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "reactive.h"

class CapacitorBase : public Reactive {
public:
    CapacitorBase( QString type, QString id );
    ~CapacitorBase();

    void setCurrentValue( double c ) override;

protected:
    double updtRes() override { return m_tStep / m_capacitance; }
    double updtCurr() override { return m_volt * m_admit; }

    double m_capacitance;
};
