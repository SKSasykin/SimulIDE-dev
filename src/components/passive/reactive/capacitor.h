/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "capacitorbase.h"

class LibraryItem;

class Capacitor : public CapacitorBase {
public:
    Capacitor( QString type, QString id );
    ~Capacitor();

    static Component* construct( QString type, QString id );
    static LibraryItem* libraryItem();

    virtual void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;
};
