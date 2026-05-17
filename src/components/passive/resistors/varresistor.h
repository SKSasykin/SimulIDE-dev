/***************************************************************************
 *   Copyright (C) 2020 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "varresbase.h"

class LibraryItem;

class VarResistor : public VarResBase {
public:
    VarResistor( QString type, QString id );
    ~VarResistor();

    static Component* construct( QString type, QString id );
    static LibraryItem* libraryItem();

    virtual void updateStep() override;

    virtual void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;
};
