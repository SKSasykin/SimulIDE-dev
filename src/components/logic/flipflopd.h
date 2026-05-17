/***************************************************************************
 *   Copyright (C) 2016 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "flipflopbase.h"

class LibraryItem;

class FlipFlopD : public FlipFlopBase {
public:
    FlipFlopD( QString type, QString id );
    ~FlipFlopD();

    static Component* construct( QString type, QString id );
    static LibraryItem* libraryItem();

protected:
    void calcOutput() override;
};
