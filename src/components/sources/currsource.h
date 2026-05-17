/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "varsource.h"

class LibraryItem;
class Pin;

class CurrSource : public VarSource {
public:
    CurrSource( QString type, QString id );
    ~CurrSource();

    static Component* construct( QString type, QString id );
    static LibraryItem* libraryItem();

    void stamp() override;
    void updateStep() override;

private:
    Pin* m_outPin;
};
