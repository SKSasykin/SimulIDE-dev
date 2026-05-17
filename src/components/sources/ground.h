/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "component.h"

class LibraryItem;

class Ground : public Component {
public:
    Ground( QString type, QString id );
    ~Ground();

    static Component* construct( QString type, QString id );
    static LibraryItem* libraryItem();

protected:
    void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;
};
