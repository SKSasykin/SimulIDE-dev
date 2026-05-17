/***************************************************************************
 *   Copyright (C) 2017 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "meter.h"

class LibraryItem;

class Amperimeter : public Meter {
public:
    Amperimeter( QString type, QString id );
    ~Amperimeter();

    static Component* construct( QString type, QString id );
    static LibraryItem* libraryItem();

    void updateStep() override;
};
