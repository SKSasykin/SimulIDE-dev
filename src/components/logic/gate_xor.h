/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "component.h"
#include "gate.h"

class LibraryItem;

class XorGate : public Gate {
public:
    XorGate( QString type, QString id );
    ~XorGate();

    static Component* construct( QString type, QString id );
    static LibraryItem* libraryItem();

    void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;

protected:
    bool calcOutput( int inputs ) override;
    void updatePath() override;
};
