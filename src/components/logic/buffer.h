/***************************************************************************
 *   Copyright (C) 2010 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "gate.h"

class LibraryItem;

class Buffer : public Gate {
public:
    Buffer( QString type, QString id );
    ~Buffer();

    static Component* construct( QString type, QString id );
    static LibraryItem* libraryItem();

    void setSmall( bool s );
    bool isSmall() { return m_small; }

    void setTristate( bool t ) override;

protected:
    void updatePath() override;

    bool m_small;
};
