/***************************************************************************
 *   Copyright (C) 2016 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "flipflopbase.h"

class LibraryItem;

class FlipFlopRS : public FlipFlopBase {
public:
    FlipFlopRS( QString type, QString id );
    ~FlipFlopRS();

    static Component* construct( QString type, QString id );
    static LibraryItem* libraryItem();

    void voltChanged() override;
};
