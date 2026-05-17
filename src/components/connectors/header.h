/***************************************************************************
 *   Copyright (C) 2022 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include "connbase.h"

class LibraryItem;

class Header : public ConnBase {
public:
    Header( QString type, QString id );
    ~Header();

    static Component* construct( QString type, QString id );
    static LibraryItem* libraryItem();

    bool pointDown() { return m_pointDown; }
    void setPointDown( bool down );

    void updatePins() override;

private:
    void updatePixmap() override;

    bool m_pointDown;
};
