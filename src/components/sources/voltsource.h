/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "varsource.h"

class LibraryItem;
class IoPin;

class VoltSource : public VarSource {
public:
    VoltSource( QString type, QString id );
    ~VoltSource();

    static Component* construct( QString type, QString id );
    static LibraryItem* libraryItem();

    void updateStep() override;

private:
    IoPin* m_outPin;
};
