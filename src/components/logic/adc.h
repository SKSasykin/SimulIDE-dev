/***************************************************************************
 *   Copyright (C) 2017 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "logiccomponent.h"

class LibraryItem;

class ADC : public LogicComponent {
public:
    ADC( QString type, QString id );
    ~ADC();

    static Component* construct( QString type, QString id );
    static LibraryItem* libraryItem();

    void stamp() override;
    void voltChanged() override;
    void runEvent() override { IoComponent::runOutputs(); }

    double maxVolt() { return m_maxVolt; }
    void setMaxVolt( double v );

    void setNumOutputs( int pins );

protected:
    double m_ratio;
    double m_maxVolt;
    uint m_maxValue;
};
