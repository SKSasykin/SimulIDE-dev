/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "meter.h"

class LibraryItem;

class Voltimeter : public Meter {
public:
    Voltimeter( QString type, QString id );
    ~Voltimeter();

    static Component* construct( QString type, QString id );
    static LibraryItem* libraryItem();

    void updateStep() override;
};
