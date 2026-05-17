/***************************************************************************
 *   Copyright (C) 2018 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "shape.h"

class LibraryItem;

class Rectangle : public Shape {
public:
    Rectangle( QString type, QString id );
    ~Rectangle();

    static Component* construct( QString type, QString id );
    static LibraryItem* libraryItem();

    void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;
};
