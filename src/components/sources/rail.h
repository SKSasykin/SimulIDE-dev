/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "component.h"
#include "e-element.h"

class LibraryItem;
class IoPin;

class Rail : public Component, public eElement {
public:
    Rail( QString type, QString id );
    ~Rail();

    static Component* construct( QString type, QString id );
    static LibraryItem* libraryItem();

    double volt() { return m_volt; }
    void setVolt( double v );

    void stamp() override;

protected:
    QPainterPath shape() const override;
    void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;

private:
    double m_volt;

    IoPin* m_out;
};
