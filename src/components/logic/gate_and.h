/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "component.h"
#include "gate.h"

class LibraryItem;

class AndGate : public Gate {
public:
    AndGate( QString type, QString id );
    ~AndGate();

    static Component* construct( QString type, QString id );
    static LibraryItem* libraryItem();

protected:
    void updatePath() override;
};
