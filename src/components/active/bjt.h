/***************************************************************************
 *   Copyright (C) 2018 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "component.h"
#include "e-bjt.h"

class LibraryItem;

class BJT : public Component, public eBJT {
public:
    BJT( QString type, QString id );
    ~BJT();

    static Component* construct( QString type, QString id );
    static LibraryItem* libraryItem();

    void updateStep() override;

    void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;
};
