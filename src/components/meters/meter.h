/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "component.h"
#include "e-resistor.h"

class IoPin;

class Meter : public Component, public eResistor {
public:
    Meter( QString type, QString id );
    ~Meter();

    bool propNotFound( QString prop, QString val ) override;

    bool swithchPins() { return m_switchPins; }
    void setSwitchPins( bool s );

    void initialize() override { m_crashed = false; }
    void updateStep() override;

    void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;

protected:
    void setflip() override;

    QString m_unit;
    double m_dispValue;
    bool m_switchPins;

    IoPin* m_outPin;
    QGraphicsSimpleTextItem m_display;
};
