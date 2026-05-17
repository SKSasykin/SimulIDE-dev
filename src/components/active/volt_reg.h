/***************************************************************************
 *   Copyright (C) 2018 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "component.h"
#include "e-resistor.h"

class LibraryItem;

class VoltReg : public Component, public eResistor {
public:
    VoltReg( QString type, QString id );
    ~VoltReg();

    static Component* construct( QString type, QString id );
    static LibraryItem* libraryItem();

    void stamp() override;
    void updateStep() override;
    void voltChanged() override;

    double outVolt() { return m_vRef; }
    void setOutVolt( double v ) {
        m_vRef = v;
        m_changed = true;
    }

    void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w );

private:
    double m_vRef;
    double m_lastCurrent;

    bool m_connected;
};
