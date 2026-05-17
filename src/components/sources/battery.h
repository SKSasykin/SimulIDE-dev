/***************************************************************************
 *   Copyright (C) 2020 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "comp2pin.h"
#include "e-resistor.h"

class LibraryItem;

class Battery : public Comp2Pin, public eResistor {
public:
    Battery( QString type, QString id );
    ~Battery();

    static Component* construct( QString type, QString id );
    static LibraryItem* libraryItem();

    double voltage() { return m_volt; }
    void setVoltage( double volt );

    void setResistance( double resist ) override;

    void stamp() override;
    void updateStep() override;

protected:
    void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;

private:
    double m_volt;

    bool m_connected;
};
