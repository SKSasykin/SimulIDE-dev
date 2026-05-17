/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "comp2pin.h"
#include "e-resistor.h"

class LibraryItem;

class Resistor : public Comp2Pin, public eResistor {
public:
    Resistor( QString type, QString id );
    ~Resistor();

    static Component* construct( QString type, QString id );
    static LibraryItem* libraryItem();

    static void drawAnsi( QPainter* p, int x, int y, double sX = 1, double sY = 1 );

    void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;
};
