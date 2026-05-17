/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "capacitorbase.h"

class LibraryItem;

class elCapacitor : public CapacitorBase {
public:
    elCapacitor( QString type, QString id );
    ~elCapacitor();

    static Component* construct( QString type, QString id );
    static LibraryItem* libraryItem();

    virtual void updateStep() override;

    virtual void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;
};
